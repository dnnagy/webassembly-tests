(module 
 (import "console" "log" (func $log (param i32))) ;; Import javascript console and log a number to it
 (func (export "logi32") (param $p i32)
    local.get $p
    call $log
 )
)