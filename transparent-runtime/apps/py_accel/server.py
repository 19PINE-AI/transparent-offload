import os, ctypes, asyncio, concurrent.futures
from aiohttp import web

lib = ctypes.CDLL(os.environ.get("ACCEL_LIB", "libaccel.so"))
lib.accel_encrypt.argtypes = [ctypes.c_void_p, ctypes.c_int]
EXEC = concurrent.futures.ThreadPoolExecutor(max_workers=32)

def offload():
    b = ctypes.create_string_buffer(4096)
    lib.accel_encrypt(b, 4096)          # ctypes releases the GIL during the C call

async def sync_h(req):
    offload()                            # runs on the event-loop thread -> blocks it
    return web.Response(text="OK")

async def async_h(req):
    await asyncio.get_event_loop().run_in_executor(EXEC, offload)  # thread pool -> overlap
    return web.Response(text="OK")

app = web.Application()
app.add_routes([web.get('/sync', sync_h), web.get('/async', async_h)])
web.run_app(app, host='127.0.0.1', port=7790, access_log=None)
