const char *MAINPAGE = R"html(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>C21.20 Garden</title><link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-QWTKZyjpPEjISv5WaRU9OFeRpok6YctnYmDr5pNlyT2bRjXh0JMhjY6hW+ALEwIH" crossorigin="anonymous"><style>.main-content{margin-top:40px;max-width:600px}.loading{position:fixed;top:0;left:0;width:100%;height:100%;z-index:99999;backdrop-filter:blur(3px);display:flex;justify-content:center;align-items:center}.loading-container{width:max-content;height:max-content;padding:20px;border-radius:18px;display:flex;justify-content:center;align-items:center;background:rgba(0 0 0 / 25%)}.card{border-radius:18px;border-width:0;box-shadow:0 0 10px rgba(0,0,0,.1);margin-bottom:40px}.form-switch input{height:2em!important;width:4em!important}.form-switch label{margin-top:3px!important;margin-left:14px!important;font-size:1.4em!important}.form-range::-webkit-slider-runnable-track{background-color:transparent}.form-range:disabled{visibility:hidden}</style></head><body><div class="d-none loading fade"><div class="loading-container"><span class="text-white fs-4 fade" id="notify-text"></span><div class="spinner-border text-white" style="width:80px;height:80px" role="status"><span class="visually-hidden">Loading...</span></div></div></div><div class="container-fluid"><div class="row justify-content-center"><div class="main-content col"><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="VALVE" checked-label="OPEN" uncheck-label="CLOSE"><label class="form-check-label" for="flexSwitchCheckDefault">VALVE</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="WATER_LEAK" disabled="disabled"><label class="form-check-label" for="flexSwitchCheckDefault">WATER LEAK</label></div></div></div><hr class="my-5"><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R1"><label class="form-check-label" for="flexSwitchCheckDefault">R1</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R2"><label class="form-check-label" for="flexSwitchCheckDefault">R2</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R3"><label class="form-check-label" for="flexSwitchCheckDefault">R3</label></div></div></div><div class="card"><div class="card-body"><div class="form-check form-switch"><input class="form-check-input" type="checkbox" role="switch" id="R4"><label class="form-check-label" for="flexSwitchCheckDefault">R4</label></div></div></div></div></div></div><script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js" integrity="sha384-YvpcrYf0tY3lHB60NNkmXc5s9fDVZLESaAA55NDzOxhy9GkcIdslK1eN7N6jIeHz" crossorigin="anonymous"></script><script>const LOADING = new class {
    constructor() {
        this.el = document.querySelector('.loading')
        this.spiner = this.el.querySelector('.spinner-border')
        this.text = this.el.querySelector('#notify-text')
        this.showDelay = null
        this.showCB = null
        this.hideDelay = null
    }
    show(timeout = 0, timeoutCB = null) {
        this.el.classList.remove('d-none')
        this.spiner.classList.remove('d-none')
        this.text.innerHTML = ''
        this.text.classList.remove('show')
        clearTimeout(this.showDelay)
        clearTimeout(this.showCB)
        clearTimeout(this.hideDelay)
        this.showDelay = setTimeout(() => this.el.classList.add('show'), 100)
        if (timeout) {
            this.showCB = setTimeout(() => {
                this.hide()
                if (timeoutCB) timeoutCB()
            }, timeout)
        }
    }
    hide() {
        this.el.classList.remove('show')
        clearTimeout(this.showDelay)
        clearTimeout(this.showCB)
        clearTimeout(this.hideDelay)
        this.hideDelay = setTimeout(() => this.el.classList.add('d-none'), 100)
    }
    setText(text, timeout = 0, timeoutCB = null) {
        this.show()
        this.text.innerHTML = text
        this.spiner.classList.add('d-none')
        this.text.classList.add('show')
        if (timeout) {
            this.showCB = setTimeout(() => {
                this.hide()
                if (timeoutCB) timeoutCB()
            }, timeout)
        }
    }
}
function setSwitchState(id, state) {
    const el = document.getElementById(id)
    if (!el) return
    ['ON', 'OPEN', 'LEAK'].some(s => s == state) ? el.checked = true : el.checked = false
}</script><script>var ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (e) => {
    LOADING.hide()
    const message = e.data
    console.log('[WS Received]', message)
    if (!message) return
    message.split('\n')
        .forEach(m => {
            if (!m.includes(':')) return
            const [id, state] = m.split(':')
            setSwitchState(id, state)
        });
}
document.querySelectorAll('.form-switch input').forEach(i => {
    i.addEventListener('change', e => {
        console.log(e.target.id, e.target.checked)
        LOADING.show(2000, () => {
            LOADING.setText('Lỗi: điều khiển thất bại', 2000)
            e.target.checked = !e.target.checked
        })
        let state = ''
        if (e.target.checked) {
            state = e.target.getAttribute('checked-label') || 'ON'
        } else {
            state = e.target.getAttribute('uncheck-label') || 'OFF'
        }
        ws.send(`${e.target.id}:${state}`)
    })
})</script></body></html>
)html";