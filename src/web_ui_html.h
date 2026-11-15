#pragma once

// Settings page served by the Pico over the USB network interface.
// Kept deliberately small: it only needs a browser, no external resources.
static const char web_ui_html[] = R"HTML(<!doctype html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pico-ASHA</title>
<style>
body{font-family:system-ui,sans-serif;max-width:30rem;margin:1rem auto;padding:0 1rem}
label{display:block;margin-top:1rem;font-weight:600}
input,select,button{width:100%;font-size:1rem;padding:.5rem;margin-top:.25rem;box-sizing:border-box}
small{color:#555;display:block;font-weight:400}
button{margin-top:1.5rem}
#msg{margin-top:1rem}
</style></head><body>
<h1>Pico-ASHA</h1>
<p id="ver"></p>
<label>Version USB audio
<select id="uac"><option value="1">UAC1</option><option value="2">UAC2</option></select></label>
<label>Min Volume (pas de 0,375 dB)
<input id="min" type="number" min="-240" max="0">
<small>Niveau en bas du curseur de volume de l’ordinateur.</small></label>
<label>Max Volume (pas de 0,375 dB)
<input id="max" type="number" min="-240" max="0">
<small>Niveau à 100 %.</small></label>
<label>Atténuation numérique sous (pas de 0,375 dB)
<input id="thr" type="number" min="-240" max="-1">
<small>En dessous de ce niveau, le volume des appareils reste fixe et le signal est atténué numériquement.</small></label>
<button id="save">Enregistrer et redémarrer la Pico</button>
<p id="msg"></p>
<script>
const $=i=>document.getElementById(i);
async function load(){
  const s=await (await fetch('/api/settings')).json();
  $('uac').value=s.uac;$('min').value=s.min;$('max').value=s.max;$('thr').value=s.thr;
  $('ver').textContent='Firmware '+s.fw;
}
async function save(){
  const min=+$('min').value,max=+$('max').value,thr=+$('thr').value;
  if(!(min>=-240&&max<=0&&min<=max&&thr>=-240&&thr<0)){$('msg').textContent='Valeurs invalides.';return;}
  const body='uac='+$('uac').value+'&min='+min+'&max='+max+'&thr='+thr;
  try{
    const r=await fetch('/api/set',{method:'POST',headers:{'X-Pico-ASHA':'1'},body});
    $('msg').textContent=r.ok?'Enregistré. La Pico redémarre (quelques secondes).':'Réglages refusés par la Pico.';
  }catch(e){$('msg').textContent='Connexion perdue : la Pico redémarre.';}
}
$('save').onclick=save;
load().catch(()=>{$('msg').textContent='Impossible de lire les réglages.';});
</script></body></html>)HTML";
