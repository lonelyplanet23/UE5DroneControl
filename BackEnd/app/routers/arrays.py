import logging
from fastapi import APIRouter, HTTPException, Request
from app.models import ArrayTask

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/arrays", tags=["arrays"])

_array_counter = 0


@router.post("", status_code=202)
def create_array(body: ArrayTask, request: Request):
    global _array_counter
    _array_counter += 1
    array_id = f"a{_array_counter}"
    logger.info("[STUB] array task %s: mode=%s slots=%d", array_id, body.mode, len(body.slots))
    return {"array_id": array_id, "status": "ASSEMBLING"}


@router.post("/{array_id}/stop")
def stop_array(array_id: str):
    logger.info("[STUB] stop array %s", array_id)
    return {"array_id": array_id, "status": "STOPPED"}
