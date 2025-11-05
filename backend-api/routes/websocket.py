"""
WebSocket routes for real-time data streaming
"""

from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Depends
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
from datetime import datetime, timedelta
import json
from typing import Dict, Set

from database import get_db
from models import VitalsData
from routes.auth import get_current_user, oauth2_scheme

router = APIRouter()

class ConnectionManager:
    """Manages WebSocket connections"""
    
    def __init__(self):
        self.active_connections: Dict[int, Set[WebSocket]] = {}
    
    async def connect(self, websocket: WebSocket, user_id: int):
        """Connect a WebSocket for a user"""
        await websocket.accept()
        if user_id not in self.active_connections:
            self.active_connections[user_id] = set()
        self.active_connections[user_id].add(websocket)
    
    def disconnect(self, websocket: WebSocket, user_id: int):
        """Disconnect a WebSocket"""
        if user_id in self.active_connections:
            self.active_connections[user_id].discard(websocket)
            if not self.active_connections[user_id]:
                del self.active_connections[user_id]
    
    async def send_personal_message(self, message: dict, websocket: WebSocket):
        """Send message to a specific connection"""
        await websocket.send_json(message)
    
    async def broadcast_to_user(self, message: dict, user_id: int):
        """Broadcast message to all connections of a user"""
        if user_id in self.active_connections:
            disconnected = set()
            for connection in self.active_connections[user_id]:
                try:
                    await connection.send_json(message)
                except Exception:
                    disconnected.add(connection)
            # Clean up disconnected connections
            for conn in disconnected:
                self.disconnect(conn, user_id)

manager = ConnectionManager()

@router.websocket("/ws/{user_id}")
async def websocket_endpoint(websocket: WebSocket, user_id: int):
    """WebSocket endpoint for real-time vitals streaming"""
    await manager.connect(websocket, user_id)
    try:
        while True:
            # Wait for client messages (optional - can be used for commands)
            data = await websocket.receive_text()
            # Echo back or process command
            await websocket.send_json({"message": f"Received: {data}"})
    except WebSocketDisconnect:
        manager.disconnect(websocket, user_id)

@router.post("/ws/broadcast/{user_id}")
async def broadcast_vitals(user_id: int, vitals_data: dict, db: AsyncSession = Depends(get_db)):
    """Broadcast vitals data to all connected WebSocket clients for a user"""
    await manager.broadcast_to_user(vitals_data, user_id)
    return {"status": "broadcasted", "user_id": user_id}

@router.websocket("/ws/live/{user_id}")
async def live_vitals_stream(websocket: WebSocket, user_id: int, db: AsyncSession = Depends(get_db)):
    """Live vitals streaming endpoint"""
    await manager.connect(websocket, user_id)
    try:
        # Send initial data
        cutoff_time = datetime.utcnow() - timedelta(minutes=5)
        result = await db.execute(
            select(VitalsData)
            .where(VitalsData.user_id == user_id)
            .where(VitalsData.timestamp >= cutoff_time)
            .order_by(VitalsData.timestamp.desc())
            .limit(10)
        )
        recent_vitals = result.scalars().all()
        
        for vital in reversed(recent_vitals):
            await websocket.send_json({
                "heart_rate": vital.heart_rate,
                "spo2": vital.spo2,
                "temperature": vital.temperature,
                "steps": vital.steps,
                "timestamp": vital.timestamp.isoformat(),
            })
        
        # Keep connection alive and stream updates
        while True:
            await websocket.receive_text()  # Wait for ping or keep-alive
            # In production, this would be triggered by new data events
    except WebSocketDisconnect:
        manager.disconnect(websocket, user_id)

