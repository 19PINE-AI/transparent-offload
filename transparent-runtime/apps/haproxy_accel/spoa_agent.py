import asyncio, ctypes, os, concurrent.futures
from haproxyspoa.spoa_server import SpoaServer
from haproxyspoa.payloads.ack import AckPayload

lib = ctypes.CDLL(os.environ.get("ACCEL_LIB", "libaccel.so"))
lib.accel_encrypt.argtypes = [ctypes.c_void_p, ctypes.c_int]
EXEC = concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get("SPOA_WORKERS","64")))

def offload():
    b = ctypes.create_string_buffer(4096)
    lib.accel_encrypt(b, 4096)          # the offload (blocks one executor thread)

spoa = SpoaServer()

@spoa.handler("offload")
async def on_offload(**kwargs):
    # run the offload on the thread pool; the agent's event loop stays free -> overlap
    await asyncio.get_event_loop().run_in_executor(EXEC, offload)
    return AckPayload()

spoa.run(host="127.0.0.1", port=9002)
