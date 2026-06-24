CREATE FUNCTION accel_sync(integer)  RETURNS integer AS '$libdir/accel_ext' LANGUAGE C STRICT;
CREATE FUNCTION accel_async(integer) RETURNS integer AS '$libdir/accel_ext' LANGUAGE C STRICT;
