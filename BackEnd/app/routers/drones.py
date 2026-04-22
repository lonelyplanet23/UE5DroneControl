from fastapi import APIRouter, HTTPException, Request
from app.models import RegisterRequest, UpdateRequest

router = APIRouter(prefix="/api/drones", tags=["drones"])


def get_reg(request: Request):
    return request.app.state.registry


@router.get("")
def list_drones(request: Request):
    return get_reg(request).list_drones()


@router.post("", status_code=201)
def register_drone(body: RegisterRequest, request: Request):
    try:
        return get_reg(request).register(body)
    except ValueError as e:
        raise HTTPException(status_code=409, detail=str(e))


@router.put("/{drone_id}")
def update_drone(drone_id: str, body: UpdateRequest, request: Request):
    try:
        return get_reg(request).update(drone_id, body)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))


@router.delete("/{drone_id}", status_code=204)
def delete_drone(drone_id: str, request: Request):
    try:
        get_reg(request).delete(drone_id)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))


@router.get("/{drone_id}/anchor")
def get_anchor(drone_id: str, request: Request):
    try:
        d = get_reg(request).get(drone_id)
        return {"lat": d.gps_lat, "lon": d.gps_lon, "alt": d.gps_alt}
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))
