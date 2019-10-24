;; https://developer.mozilla.org/en-US/docs/WebAssembly/Understanding_the_text_format
;; https://developer.mozilla.org/en-US/docs/WebAssembly/Text_format_to_wasm
(module
  (func $add (param $lhs f64) (param $rhs f64) (result f64)
    local.get $lhs
    local.get $rhs
    f64.add)
  (export "add" (func $add))
)