package.cpath = "/home/ubuntu/transparent-offload/transparent-runtime/apps/haproxy_accel/?.so;" .. package.cpath
local accel = require("accelmod")
core.register_service("accel", "http", function(applet)
    accel.offload()
    local body = "OK"
    applet:set_status(200)
    applet:add_header("content-length", tostring(#body))
    applet:start_response()
    applet:send(body)
end)
