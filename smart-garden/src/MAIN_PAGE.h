const char *MAINPAGE = R"html(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>C21.20 Garden</title><link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-QWTKZyjpPEjISv5WaRU9OFeRpok6YctnYmDr5pNlyT2bRjXh0JMhjY6hW+ALEwIH" crossorigin="anonymous"><style>.main-content{margin-top:40px;max-width:600px}.loading{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0 0 0 / 25%);z-index:99999;backdrop-filter:blur(3px);display:flex;justify-content:center;align-items:center}.card{border-radius:18px;border-width:0;box-shadow:0 0 10px rgba(0,0,0,.1);margin-bottom:40px}.form-switch input{height:2em!important;width:4em!important}.form-switch label{margin-top:3px!important;margin-left:14px!important;font-size:1.4em!important}</style></head><body><div class="loading d-none"><div class="spinner-border text-white" style="width:80px;height:80px" role="status"><span class="visually-hidden">Loading...</span></div></div><div class="container-fluid"><div class="row justify-content-center"><div class="main-content col"><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R1"><label class="form-check-label" for="flexSwitchCheckDefault">R1</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R2"><label class="form-check-label" for="flexSwitchCheckDefault">R2</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R3"><label class="form-check-label" for="flexSwitchCheckDefault">R3</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R4"><label class="form-check-label" for="flexSwitchCheckDefault">R4</label></div></div></div></div></div></div><script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js" integrity="sha384-YvpcrYf0tY3lHB60NNkmXc5s9fDVZLESaAA55NDzOxhy9GkcIdslK1eN7N6jIeHz" crossorigin="anonymous"></script><script>function setSwitchState(e,t){document.getElementById(e).checked="ON"==t}</script><script>var ws = new WebSocket(`ws://${location.host}/ws`);
var $loading = document.querySelector('.loading')
ws.onmessage = (e) => {
    $loading.classList.add('d-none')
    const message = e.data
    console.log(message)
    if (!message) return
    message.split('\n')
        .forEach(m => {
            const match = m.match(/R\d{1}:(ON|OFF)/g)
            if (match) {
                const [id, state] = match[0].split(':')
                setSwitchState(id, state)
            }
        });
}

document.querySelectorAll('.form-switch input').forEach(i => {
    i.addEventListener('change', e => {
        console.log(e.target.id, e.target.checked)
        $loading.classList.remove('d-none')
        ws.send(`${e.target.id}:${e.target.checked ? 'ON' : 'OFF'}`)
        setTimeout(() => {
            $loading.classList.add('d-none')
        }, 1000)
    })
})</script></body></html>
)html";