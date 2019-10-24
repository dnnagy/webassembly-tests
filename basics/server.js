const express = require('express')
express.static.mime.define({'application/wasm': ['wasm']})
var app = express();

app.use('/', express.static('.'));

app.listen(8080, function () {
  console.log('Example app listening on port 8080!')
})