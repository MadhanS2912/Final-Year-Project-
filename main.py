from fastapi import FastAPI, HTTPException, Depends
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from typing import Dict, List
import asyncio
import json
import os
import random
import string
from datetime import datetime, timezone
from zoneinfo import ZoneInfo
import httpx
import base64
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.backends import default_backend
from motor.motor_asyncio import AsyncIOMotorClient
import secrets
from dotenv import load_dotenv
from hashlib import sha256
from typing import List

app = FastAPI(title="Encrypted Chat Server", version="1.0.0")
load_dotenv()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "https://agnicom.netlify.app"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- Configuration ---
MONGODB_URL = os.getenv("MONGODB_URI")
DATABASE_NAME = "agnidb"
PINATA_API_KEY = os.getenv("PINATA_API_KEY")
PINATA_SECRET_API_KEY = os.getenv("PINATA_SECRET_API_KEY")
PINATA_JWT = os.getenv("PINATA_JWT")

client = AsyncIOMotorClient(MONGODB_URL)
db = client[DATABASE_NAME]
users_collection = db.users
messages_collection = db.messages
notifications_collection = db.notifications

# --- Models ---
class UserCreate(BaseModel):
    username: str
    pin: str = Field(..., min_length=4, max_length=6, description="Numeric PIN for login")

class User(BaseModel):
    user_id: str
    username: str
    public_key: str

class MessageSend(BaseModel):
    recipient_user_id: str
    message: str

class MessageResponse(BaseModel):
    message_id: str
    sender_user_id: str
    recipient_user_id: str
    encrypted_for_recipient: str
    encrypted_for_sender: str
    ipfs_hash: str
    timestamp: datetime

class NotificationResponse(BaseModel):
    notification_id: str
    user_id: str
    message: str
    timestamp: datetime
    read: bool

class UserLogin(BaseModel):
    user_id: str
    pin: str

# --- Encryption ---
class EncryptionManager:
    @staticmethod
    def generate_ecc_keypair():
        private_key = ec.generate_private_key(ec.SECP256R1(), default_backend())
        public_key = private_key.public_key()

        private_pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        )
        public_pem = public_key.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo
        )
        return private_pem, public_pem

    @staticmethod
    def encrypt_message(message: str, recipient_public_key_pem: bytes) -> tuple:
        recipient_public_key = serialization.load_pem_public_key(
            recipient_public_key_pem, backend=default_backend()
        )

        ephemeral_private_key = ec.generate_private_key(ec.SECP256R1(), default_backend())
        ephemeral_public_key = ephemeral_private_key.public_key()

        shared_key = ephemeral_private_key.exchange(ec.ECDH(), recipient_public_key)

        derived_key = HKDF(
            algorithm=hashes.SHA256(),
            length=32,
            salt=None,
            info=b'chat-encryption',
            backend=default_backend()
        ).derive(shared_key)

        iv = secrets.token_bytes(16)
        cipher = Cipher(algorithms.AES(derived_key), modes.CBC(iv), backend=default_backend())
        encryptor = cipher.encryptor()

        message_bytes = message.encode('utf-8')
        padding_length = 16 - (len(message_bytes) % 16)
        padded_message = message_bytes + bytes([padding_length]) * padding_length

        encrypted_message = encryptor.update(padded_message) + encryptor.finalize()

        ephemeral_public_pem = ephemeral_public_key.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo
        )

        encrypted_data = {
            "ephemeral_public_key": base64.b64encode(ephemeral_public_pem).decode(),
            "iv": base64.b64encode(iv).decode(),
            "encrypted_message": base64.b64encode(encrypted_message).decode()
        }

        return json.dumps(encrypted_data), ephemeral_public_pem

    @staticmethod
    def decrypt_message(encrypted_data: str, private_key_pem: bytes) -> str:
        data = json.loads(encrypted_data)
        private_key = serialization.load_pem_private_key(
            private_key_pem, password=None, backend=default_backend()
        )
        ephemeral_public_key = serialization.load_pem_public_key(
            base64.b64decode(data["ephemeral_public_key"]), backend=default_backend()
        )

        shared_key = private_key.exchange(ec.ECDH(), ephemeral_public_key)

        derived_key = HKDF(
            algorithm=hashes.SHA256(),
            length=32,
            salt=None,
            info=b'chat-encryption',
            backend=default_backend()
        ).derive(shared_key)

        iv = base64.b64decode(data["iv"])
        encrypted_message = base64.b64decode(data["encrypted_message"])

        cipher = Cipher(algorithms.AES(derived_key), modes.CBC(iv), backend=default_backend())
        decryptor = cipher.decryptor()
        decrypted_padded = decryptor.update(encrypted_message) + decryptor.finalize()

        padding_length = decrypted_padded[-1]
        decrypted_message = decrypted_padded[:-padding_length]

        return decrypted_message.decode('utf-8')

# --- Pinata ---
class PinataManager:
    @staticmethod
    async def upload_to_pinata(data: str) -> str:
        if not PINATA_JWT:
            raise HTTPException(status_code=500, detail="Pinata JWT not configured")

        url = "https://api.pinata.cloud/pinning/pinJSONToIPFS"
        headers = {
            "Authorization": f"Bearer {PINATA_JWT}",
            "Content-Type": "application/json"
        }
        indian_time = datetime.now(ZoneInfo("Asia/Kolkata"))

        payload = {
            "pinataContent": {
                "encrypted_data": data,
                "timestamp": indian_time.strftime("%Y-%m-%d %H:%M:%S")
            },
            "pinataMetadata": {
                "name": f"chat_message_{secrets.token_hex(8)}"
            }
        }

        async with httpx.AsyncClient() as client:
            response = await client.post(url, json=payload, headers=headers)
            if response.status_code != 200:
                raise HTTPException(status_code=500, detail="Failed to upload to Pinata")
            return response.json()["IpfsHash"]

# --- Utility ---
def generate_user_id() -> str:
    return ''.join(random.choices(string.ascii_letters + string.digits, k=12))

def generate_message_id() -> str:
    return ''.join(random.choices(string.ascii_letters + string.digits, k=16))

# --- Routes ---
@app.post("/users", response_model=User)
async def create_user(user_data: UserCreate):
    user_id = generate_user_id()
    while await users_collection.find_one({"user_id": user_id}):
        user_id = generate_user_id()

    pin_hash = sha256(user_data.pin.encode()).hexdigest()
    private_key_pem, public_key_pem = EncryptionManager.generate_ecc_keypair()

    user = {
        "user_id": user_id,
        "username": user_data.username,
        "public_key": public_key_pem.decode(),
        "private_key": private_key_pem.decode(),
        "pin_hash": pin_hash,
        "created_at": datetime.now(timezone.utc)
    }
    await users_collection.insert_one(user)

    return User(user_id=user_id, username=user_data.username, public_key=public_key_pem.decode())

@app.post("/login")
async def login_user(login_data: UserLogin):
    user = await users_collection.find_one({"user_id": login_data.user_id})
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    pin_hash = sha256(login_data.pin.encode()).hexdigest()
    if user["pin_hash"] != pin_hash:
        raise HTTPException(status_code=401, detail="Invalid PIN")

    return {"message": "Login successful", "user_id": user["user_id"], "username": user["username"]}

@app.get("/users")
async def get_all_users():
    users = await users_collection.find({}, {"_id": 0, "private_key": 0, "pin_hash": 0}).to_list(None)
    return {"users": users}

@app.get("/users/{user_id}", response_model=User)
async def get_user(user_id: str):
    user = await users_collection.find_one({"user_id": user_id})
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    return User(user_id=user["user_id"], username=user["username"], public_key=user["public_key"])

@app.post("/messages", response_model=MessageResponse)
async def send_message(message_data: MessageSend, sender_user_id: str):
    sender = await users_collection.find_one({"user_id": sender_user_id})
    if not sender:
        raise HTTPException(status_code=404, detail="Sender not found")

    recipient = await users_collection.find_one({"user_id": message_data.recipient_user_id})
    if not recipient:
        raise HTTPException(status_code=404, detail="Recipient not found")

    # Encrypt for recipient
    encrypted_for_recipient, _ = EncryptionManager.encrypt_message(
        message_data.message, recipient["public_key"].encode()
    )

    # Encrypt for sender
    encrypted_for_sender, _ = EncryptionManager.encrypt_message(
        message_data.message, sender["public_key"].encode()
    )

    ipfs_hash = await PinataManager.upload_to_pinata(encrypted_for_recipient)
    message_id = generate_message_id()

    message = {
        "message_id": message_id,
        "sender_user_id": sender_user_id,
        "recipient_user_id": message_data.recipient_user_id,
        "encrypted_for_recipient": encrypted_for_recipient,
        "encrypted_for_sender": encrypted_for_sender,
        "ipfs_hash": ipfs_hash,
        "timestamp": datetime.now(timezone.utc)
    }
    await messages_collection.insert_one(message)

    notification = {
        "notification_id": generate_message_id(),
        "user_id": message_data.recipient_user_id,
        "message": f"New message from {sender['username']}",
        "message_id": message_id,
        "timestamp": datetime.now(timezone.utc),
        "read": False
    }
    await notifications_collection.insert_one(notification)

    return MessageResponse(
        message_id=message_id,
        sender_user_id=sender_user_id,
        recipient_user_id=message_data.recipient_user_id,
        encrypted_for_recipient=encrypted_for_recipient,
        encrypted_for_sender=encrypted_for_sender,
        ipfs_hash=ipfs_hash,
        timestamp=message["timestamp"]
    )


@app.get("/messages/{sender_user_id}/{recipient_user_id}", response_model=List[MessageResponse])
async def get_user_messages(sender_user_id: str, recipient_user_id: str):
    limit = 50
    messages = await messages_collection.find({
        "$or": [
            {"$and": [
                {"sender_user_id": sender_user_id},
                {"recipient_user_id": recipient_user_id}
            ]},
            {"$and": [
                {"sender_user_id": recipient_user_id},
                {"recipient_user_id": sender_user_id}
            ]}
        ]
    }).sort("timestamp", -1).limit(limit).to_list(limit)

    # Map encrypted_message dynamically
    for msg in messages:
        if msg["recipient_user_id"] == sender_user_id:
            msg["encrypted_message"] = msg["encrypted_for_recipient"]
        else:
            msg["encrypted_message"] = msg["encrypted_for_sender"]

    return messages

@app.post("/messages/{message_id}/decrypt")
async def decrypt_message(message_id: str, user_id: str):
    message = await messages_collection.find_one({"message_id": message_id})
    if not message:
        raise HTTPException(status_code=404, detail="Message not found")

    user = await users_collection.find_one({"user_id": user_id})
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    # Choose correct encrypted version
    if message["recipient_user_id"] == user_id:
        encrypted = message["encrypted_for_recipient"]
    elif message["sender_user_id"] == user_id:
        encrypted = message["encrypted_for_sender"]
    else:
        raise HTTPException(status_code=403, detail="Not authorized to decrypt this message")

    try:
        decrypted_message = EncryptionManager.decrypt_message(
            encrypted, user["private_key"].encode()
        )
        return {"decrypted_message": decrypted_message}
    except Exception:
        raise HTTPException(status_code=500, detail="Failed to decrypt message")


@app.get("/notifications/{user_id}")
async def get_notifications(user_id: str, limit: int = 20):
    notifications = await notifications_collection.find({
        "user_id": user_id
    }).sort("timestamp", -1).limit(limit).to_list(limit)
    return notifications

@app.put("/notifications/{notification_id}/read")
async def mark_notification_read(notification_id: str, user_id: str):
    result = await notifications_collection.update_one(
        {"notification_id": notification_id, "user_id": user_id},
        {"$set": {"read": True}}
    )
    if result.matched_count == 0:
        raise HTTPException(status_code=404, detail="Notification not found")
    return {"message": "Notification marked as read"}

@app.get("/")
async def root():
    return {"message": "Encrypted Chat Server is running"}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

# Madhan: hmCRDTpQc1T0 pin:1234
# Deva: xcOWn0y2MFfq pin: 1234
