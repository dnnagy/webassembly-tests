(module
    (func (export "squaref64") (param $p f64) (result f64)
        local.get $p
        local.get $p
        f64.mul)

    (func (export "cubef64") (param $p f64) (result f64)
        (f64.mul 
            (local.get $p)
            (local.get $p))
        local.get $p
        f64.mul)
)