#pragma once

#include <pgmspace.h>

namespace web_portal {


const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#147D73">
<link rel="icon" href="data:,">
<title>Tessara</title>
<style>
:root{--red:#147D73;--ink:#172C2B;--muted:#526561;--wash:#F6F5F0;--line:#D5DFDA;--card:#fff}
*{box-sizing:border-box}
body{margin:0;font:16px/1.4 -apple-system,system-ui,sans-serif;color:var(--ink);background:#F6F5F0}
.app{width:min(480px,100%);margin:0 auto;padding:0 16px calc(28px + env(safe-area-inset-bottom))}
header{border-top:4px solid var(--red);padding:18px 0 14px;display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-end;gap:8px 24px}
h1{margin:0;font-size:26px;letter-spacing:-.02em}
header p{margin:2px 0 0;color:var(--muted);font-size:14px}
#host{font-size:14px;color:var(--ink)}
main{display:grid;gap:14px;align-items:start}
.card{background:var(--card);border-radius:18px;padding:18px}
h2{font-size:13px;font-weight:650;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);margin:0}
.note{margin:4px 0 0;color:var(--muted);font-size:14px}
.fields{display:grid;gap:16px}
.field h3{margin:0 0 8px;font-size:15px;font-weight:650}
.chips{display:flex;flex-wrap:wrap;gap:8px}
.chips button{border:0;border-radius:999px;padding:8px 14px;background:var(--wash);color:var(--ink);font-size:15px;cursor:pointer}
.chips button.on{background:var(--red);color:#fff}
label{display:block;font-size:13px;color:var(--muted);margin:0 0 4px}
input{width:100%;padding:11px 12px;border:1px solid var(--line);border-radius:10px;font-size:16px;background:#fff;color:var(--ink)}
.pair{display:grid;gap:12px;margin-top:14px}
.actions{display:flex;margin-top:14px}
.act{border:0;border-radius:10px;background:var(--red);color:#fff;padding:10px 16px;font-size:15px;cursor:pointer}
.actions .act,.head .act{margin:0}
.status{display:flex;align-items:center;gap:8px;margin:8px 0 0;font-size:14px}
.status i{width:8px;height:8px;border-radius:50%;background:#c8c8c8;display:block}
.status.on i{background:#1f9d55}
.stack{display:grid;gap:14px}
.head{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-end;gap:12px}
.board{list-style:none;margin:14px 0 0;padding:0;display:grid;gap:8px}
.board li{position:relative;display:flex;align-items:center;gap:8px;min-height:56px;padding:10px 12px;border-radius:14px;background:var(--wash)}
.board.sorting li{transition:transform .14s ease}
.idx{flex:none;width:32px;height:32px;border-radius:10px;display:grid;place-items:center;background:#fff;color:var(--muted);font-size:13px;font-weight:700;touch-action:none;cursor:grab}
.board li.lead .idx{background:var(--red);color:#fff}
.name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:650;font-size:16px;touch-action:none;cursor:grab}
.board li.off .name{color:var(--muted);font-weight:500}
.grip{flex:none;width:28px;height:40px;margin:0;padding:0;border:0;background:transparent;display:grid;grid-template-columns:4px 4px;grid-auto-rows:4px;gap:3px;align-content:center;justify-content:center;cursor:grab;touch-action:none}
.grip i{width:4px;height:4px;border-radius:50%;background:#b5b5b5;display:block}
.board li.dragging{z-index:2;background:#fff;box-shadow:0 12px 28px rgba(32,32,32,.16);transition:none}
.eye{flex:none;border:0;border-radius:999px;padding:8px 12px;background:#fff;color:var(--ink);font-size:14px;cursor:pointer}
.eye.on{background:var(--red);color:#fff}
.empty{color:var(--muted);font-weight:500}
.settings,.wifi{container-type:inline-size}
@container (min-width:440px){
.fields{grid-template-columns:1fr 1fr}
.field:nth-child(n+3){grid-column:1 / -1}
.pair{grid-template-columns:1.25fr .85fr;align-items:end}
}
.shell{display:grid;gap:16px}
.rail{display:flex;flex-direction:column;gap:6px}
.kicker{margin:14px 0 0;font-size:12px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.kicker:first-child{margin-top:0}
.nav,.jump{display:flex;flex-direction:column;align-items:flex-start;gap:2px;width:100%;text-align:left;border:1px solid var(--line);background:#fff;border-radius:14px;padding:12px 14px;cursor:pointer;color:var(--ink);font:inherit}
.jump{border:0;background:var(--wash);margin-top:12px}
.nav b,.jump b{font-size:16px;font-weight:650}
.nav span,.jump span{font-size:13px;color:var(--muted)}
.nav.on{background:var(--red);border-color:var(--red);color:#fff}
.nav.on span{color:rgba(255,255,255,.86)}
.back{display:none;border:0;background:transparent;color:var(--red);font-size:15px;padding:0 0 4px;cursor:pointer}
.page{display:none;gap:14px}
body[data-page="hub"] #page-hub,body[data-page="device"] #page-device,body[data-page="resources"] #page-resources,body[data-page="agent"] #page-agent,body[data-page="aitokens"] #page-aitokens,body[data-page="todoist"] #page-todoist,body[data-page="clima"] #page-clima{display:grid}
.meter{height:8px;background:var(--wash);border-radius:999px;overflow:hidden;margin-top:8px}
.meter>i{display:block;height:100%;background:var(--red);border-radius:inherit;width:0}
.res-grid{display:grid;gap:14px;margin-top:12px}
.res-row b{display:block;font-size:13px;font-weight:650;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
.res-row span{display:block;margin-top:4px;font-size:15px;color:var(--ink)}
.res-row em{display:block;margin-top:2px;font-style:normal;font-size:13px;color:var(--muted)}
.group{background:var(--card);border-radius:18px;padding:18px}
.need{display:none}
.need h2{color:var(--red)}
.need ol{margin:8px 0 0;padding-left:1.25em}
.need li{margin:5px 0;font-size:15px;color:var(--ink)}
.suggest{list-style:none;margin:6px 0 0;padding:0;display:grid;gap:4px}
.suggest button{width:100%;text-align:left;border:1px solid var(--line);background:var(--wash);border-radius:10px;padding:10px 12px;font:inherit;cursor:pointer;color:var(--ink)}
.suggest button b{font-weight:650}
.suggest button span{display:block;font-size:13px;color:var(--muted)}
@media(max-width:719px){
.actions .act,.head .act{width:100%}
.rail{display:none}
body:not([data-page="hub"]) .back{display:inline-flex}
}
@media(min-width:720px){
.app{width:min(860px,100%);padding:0 28px 48px}
.shell{grid-template-columns:220px minmax(0,1fr);align-items:start}
.back{display:none}
}
@media(min-width:1100px){
h1{font-size:30px}
.card{padding:22px}
}
#toast{position:fixed;left:50%;bottom:calc(16px + env(safe-area-inset-bottom));transform:translateX(-50%);background:var(--ink);color:#fff;padding:10px 16px;border-radius:10px;opacity:0;transition:opacity .2s;max-width:min(420px,calc(100% - 32px))}
.kv{display:grid;gap:10px;margin:14px 0 0}
.kv div{display:flex;justify-content:space-between;align-items:baseline;gap:12px;padding:10px 12px;border-radius:12px;background:var(--wash)}
.kv b{font-size:13px;font-weight:700;color:var(--muted);letter-spacing:.02em}
.kv span{font-size:15px;font-weight:650;text-align:right}
.kv span.ok{color:#1f9d55}.kv span.warn{color:#b45309}.kv span.off{color:var(--muted)}
.act.ghost{background:var(--wash);color:var(--ink)}
#agent-upload-box,#agent-install-box{display:none}
.cmd-row{display:grid;gap:8px;margin-top:12px}
.cmd-row code{display:block;padding:10px 12px;border-radius:10px;background:var(--wash);font:12px/1.35 ui-monospace,Menlo,monospace;word-break:break-all;color:var(--ink)}
.toggle{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 0;cursor:pointer;user-select:none}
.toggle span{font-size:15px;font-weight:650;color:var(--ink)}
/* Reset the global `input{}` padding/border/width so the thumb stays centered. */
.toggle input[type=checkbox]{-webkit-appearance:none;appearance:none;width:44px;height:26px;margin:0;padding:0;border:0;border-radius:999px;background:#c8d0cc;box-shadow:inset 0 0 0 1px rgba(23,44,43,.08);position:relative;cursor:pointer;flex:none;vertical-align:middle;transition:background .15s}
.toggle input[type=checkbox]::after{content:"";position:absolute;top:50%;left:3px;width:20px;height:20px;margin:0;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.18);transform:translateY(-50%);transition:transform .15s,left .15s}
.toggle input[type=checkbox]:checked{background:var(--red);box-shadow:none}
.toggle input[type=checkbox]:checked::after{left:21px;transform:translateY(-50%)}
.range{display:grid;gap:8px;margin-top:10px}
.range .row{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.range strong{font-size:15px;font-weight:650}
.range input[type=range]{width:100%;accent-color:var(--red)}
.ai-toggles{display:grid;gap:0;margin-top:8px}
.ai-toggles .toggle{border-top:1px solid var(--line)}
.ai-toggles .toggle:first-child{border-top:0}
</style></head><body>
<div class="app">
<header>
<div><h1>Tessara</h1><p id="who">carregando…</p></div>
<p id="host"></p>
</header>
<main class="shell">
<aside class="rail">
<button class="nav" type="button" data-go="hub"><b>Início</b><span>Home e atalhos</span></button>
<p class="kicker">Dispositivo</p>
<button class="nav" type="button" data-go="device"><b>Geral</b><span>Tela, som e Wi-Fi</span></button>
<button class="nav" type="button" data-go="resources"><b>Memória</b><span>DRAM, PSRAM, flash e SD</span></button>
<button class="nav" type="button" data-go="agent"><b>Agente Tessara</b><span>USB · status e instalação</span></button>
<p class="kicker">Widgets</p>
<button class="nav" type="button" data-go="todoist"><b>Todoist</b><span>Listas, token e intervalo</span></button>
<button class="nav" type="button" data-go="clima"><b>Clima</b><span>Cidade deste widget</span></button>
<button class="nav" type="button" data-go="aitokens"><b>AI Tokens</b><span>Fontes na home</span></button>
</aside>
<div class="pane">
<button class="back" id="back" type="button">← Configurações</button>
<div class="page" id="page-hub">
<section class="group"><h2>Dispositivo</h2><p class="note">Vale para o aparelho todo.</p>
<button class="jump" type="button" data-go="device"><b>Geral</b><span>Orientação, brilho, som, aparência e Wi-Fi</span></button>
<button class="jump" type="button" data-go="resources"><b>Memória e armazenamento</b><span>DRAM, PSRAM, firmware e volume</span></button>
<button class="jump" type="button" data-go="agent"><b>Agente Tessara</b><span>USB · status e instalação</span></button></section>
<section class="group"><h2>Widgets</h2>
<section class="card"><h2>Na home</h2>
<p class="note" id="widgets-note">Escolha o que aparece no painel. A home atualiza na hora.</p>
<ol class="board" id="widgets"></ol></section>
<p class="note">Cada widget tem a própria página de ajustes.</p>
<button class="jump" type="button" data-go="todoist"><b>Todoist</b><span>Listas, token e intervalo</span></button>
<button class="jump" type="button" data-go="clima"><b>Clima</b><span>Cidade e previsão na home</span></button>
<button class="jump" type="button" data-go="aitokens"><b>AI Tokens</b><span>Resumo das cotas</span></button></section>
</div>
<div class="page" id="page-agent">
<section class="card">
<h2>Agente Tessara</h2>
<div class="kv">
<div><b>Status</b><span id="agent-status" class="off">…</span></div>
<div><b>Pacote no SD</b><span id="agent-pack-note" class="off">…</span></div>
</div>
<p class="note" id="agent-status-hint">Atualiza sozinho a cada poucos segundos. Verde = o agente no Mac falou com o painel via USB.</p>
</section>
<section class="card" id="agent-upload-box">
<h2>Enviar pacote</h2>
<p class="note">Arquivo <code>agent.tgz</code> (gere com <code>agent/pack_sd.sh</code>). Cartão SD no aparelho.</p>
<label for="agent-file-tgz">Arquivo</label>
<input id="agent-file-tgz" type="file" accept=".tgz,.tar.gz,application/gzip">
<div class="actions"><button class="act" type="button" id="agent-upload">Enviar para o SD</button></div>
</section>
<section class="card" id="agent-install-box" style="display:none">
<h2>Instalar no Mac</h2>
<p class="note">No Terminal deste Mac: cole o comando (baixa e instala na hora). Depois do install, o <b>Status</b> acima deve ir para OK — comunicando.</p>
<div class="cmd-row">
<code id="agent-install-cmd">curl -fsSL … | bash</code>
<div class="actions" style="gap:8px;flex-wrap:wrap;margin-top:0">
<button class="act" type="button" id="agent-copy-cmd">Copiar comando</button>
<a class="act ghost" id="agent-dl" href="/agent/install.command" download="install-tessera-agent.command">Baixar .command</a>
<button class="act ghost" type="button" id="agent-reupload">Trocar pacote</button>
</div>
<p class="note" id="agent-dl-hint">Se baixar o arquivo: <code style="font-size:12px">bash ~/Downloads/install-tessera-agent.command</code> (não abra com duplo clique).</p>
</div>
</section>
</div>
<div class="page" id="page-aitokens">
<section class="card"><h2>AI Tokens</h2>
<p class="note" id="aitokens-note">Cotas vêm do Agente Tessara via USB.</p>
<p class="note" id="aitokens-updated">Última atualização: …</p>
<div class="ai-toggles">
<label class="toggle"><span>Codex na home</span><input type="checkbox" data-ai="show_aitokens_codex"></label>
<label class="toggle"><span>Claude na home</span><input type="checkbox" data-ai="show_aitokens_claude"></label>
<label class="toggle"><span>Cursor na home</span><input type="checkbox" data-ai="show_aitokens_cursor"></label>
</div>
<p class="note" style="margin-top:14px">Barras detalhadas ficam no widget. Pedir cotas: no aparelho, Configurações → AI Tokens.</p>
</section>
</div>
<div class="page" id="page-resources">
<section class="card">
<h2>Memória e armazenamento</h2>
<p class="note">Amostra ao vivo do aparelho. O maior bloco de DRAM interna importa para TLS e Wi‑Fi.</p>
<div class="res-grid" id="res-grid"></div>
</section>
</div>
<div class="page" id="page-device">
<section class="card settings"><h2>Dispositivo</h2>
<div class="fields">
<div class="field"><h3>Orientação</h3><div class="chips" data-k="portrait" data-o='[[true,"Vertical"],[false,"Horizontal"]]'></div></div>
<div class="field"><h3>Som ao concluir</h3>
<label class="toggle"><span>Ligado</span><input type="checkbox" id="sound-tog"></label>
</div>
<div class="field"><h3>Brilho</h3><div class="chips" data-k="brightness" data-o='[[30,"Baixo"],[60,"Médio"],[100,"Alto"]]'></div></div>
<div class="field"><h3>Aparência</h3><div class="chips" data-k="appearance" data-o='[[0,"Claro"],[1,"Escuro"],[2,"Auto"]]'></div></div>
<div class="field"><h3>Descanso de tela</h3>
<div class="range">
<div class="row"><label for="rest_range">Aparece após inatividade</label><strong id="rest_label">5 min</strong></div>
<input id="rest_range" type="range" min="0" max="5" step="1">
</div>
<div class="range">
<div class="row"><label for="clock_range">Relógio fica por</label><strong id="clock_label">1 min</strong></div>
<input id="clock_range" type="range" min="0" max="4" step="1">
</div>
<p class="note">Auto segue o tema claro/escuro do celular ou computador. “Até tocar” mantém o flip clock até você tocar na tela.</p>
</div>
</div><p class="note">Estas opções valem para o aparelho todo, inclusive o Wi-Fi.</p></section>
<section class="card wifi"><h2>Wi-Fi</h2><p class="status" id="link"><i></i><span>carregando…</span></p>
<div class="pair"><div><label>Rede</label><input id="ssid" autocomplete="off"></div>
<div><label>Senha</label><input id="pass" type="password" autocomplete="new-password" placeholder="em branco mantém a atual"></div></div>
<div class="actions"><button class="act" onclick="post('/api/wifi',{ssid:v('ssid'),pass:v('pass')},'Wi-Fi salvo, reconectando')">Salvar Wi-Fi</button></div></section>
</div>
<div class="page" id="page-todoist">
<section class="card need" id="need"><h2>Falta o token</h2>
<ol><li>Abra todoist.com e entre na conta.</li><li>Toque na foto, no canto superior esquerdo.</li><li>Abra Configurações, depois Integrações.</li><li>Abra Desenvolvedor e toque em Copiar token da API.</li><li>Cole o token no campo abaixo.</li></ol></section>
<section class="card"><h2>Todoist</h2>
<div class="fields"><div class="field"><h3>Atualizar listas a cada</h3><div class="chips" data-k="sync_minutes" data-scope="widget" data-widget="org.tessara.todoist" data-o='[[5,"5 min"],[15,"15 min"],[30,"30 min"],[60,"1 h"]]'></div></div></div>
<p class="note">Só este widget. O restante do aparelho fica em Dispositivo.</p></section>
<section class="card token"><h2>Token do Todoist</h2><p class="note" id="tok"></p>
<div class="pair"><div><label>Novo token</label><input id="token" type="password" autocomplete="off"></div></div>
<div class="actions"><button class="act" onclick="post('/api/token',{token:v('token')},'Token salvo')">Salvar token</button></div></section>
<section class="card lists"><div class="head"><div><h2>Listas do Todoist</h2><p class="note" id="count">carregando…</p></div>
<button class="act" onclick="post('/api/refresh',{},'Atualizando no painel')">Atualizar agora</button></div>
<ol class="board" id="lists"></ol></section>
</div>
<div class="page" id="page-clima">
<section class="card"><h2>Clima</h2>
<p class="note" id="weather-note">carregando…</p>
<div class="pair"><div><label>Cidade</label><input id="weather_city" autocomplete="off" placeholder="Digite para buscar…"></div></div>
<ul class="suggest" id="weather_suggest" hidden></ul>
<div class="actions" style="gap:8px;flex-wrap:wrap">
<button class="act" type="button" onclick="saveCity()">Salvar cidade</button>
<button class="act" type="button" onclick="useGeo()">Usar minha localização</button>
</div>
<p class="note">Digite 2+ letras para ver sugestões (Open-Meteo no navegador). GPS só funciona se a página estiver em HTTPS — em HTTP use a busca.</p></section>
</div>
</div>
</main></div>
<div id="toast"></div>
<script>
let s={};const v=i=>document.getElementById(i).value;
function showPage(name){document.body.dataset.page=name;document.querySelectorAll('.nav').forEach(b=>b.classList.toggle('on',b.dataset.go===name))}
document.querySelectorAll('[data-go]').forEach(b=>b.addEventListener('click',()=>showPage(b.dataset.go)));
document.getElementById('back').addEventListener('click',()=>showPage('hub'));
showPage('hub');
let lists=[];
let widgets=[];
function toast(t){const e=document.getElementById('toast');e.textContent=t;e.style.opacity=1;setTimeout(()=>e.style.opacity=0,1800)}
let drag=null;
function drawWidgets(){const box=document.getElementById('widgets');if(!box)return;
box.innerHTML='';
if(!widgets.length){box.innerHTML='<li class="empty">Nenhum widget registrado.</li>';return}
widgets.forEach((item,i)=>{const li=document.createElement('li');
if(!item.shown)li.className='off';
const idx=document.createElement('span');idx.className='idx';idx.textContent=i+1;
const name=document.createElement('span');name.className='name';name.textContent=item.title||item.id;
const eye=document.createElement('button');eye.type='button';eye.className='eye'+(item.shown?' on':'');
eye.textContent=item.shown?'Na home':'Oculto';eye.onclick=()=>toggleWidget(i);
li.append(idx,name,eye);box.append(li)})}
function toggleWidget(i){if(widgets[i].shown&&widgets.filter(w=>w.shown).length==1){toast('Deixe ao menos um widget');return}
widgets[i].shown=!widgets[i].shown;drawWidgets();
post('/api/dashboard',{widgets},widgets[i].shown?'Widget na home':'Widget oculto',false)}
function drawLists(){const box=document.getElementById('lists');
const shown=lists.filter(l=>l.shown).length;
document.getElementById('count').textContent=lists.length?(shown+' de '+lists.length+' na home. Arraste para ordenar — a tela muda na hora.'):'Nenhuma lista carregada';
box.innerHTML='';
if(!lists.length){box.innerHTML='<li class="empty">As listas aparecem aqui depois da primeira busca.</li>';return}
let lead=false;
lists.forEach(item=>{const li=document.createElement('li');
if(!item.shown)li.className='off';else if(!lead){li.className='lead';lead=true}
const grip=document.createElement('button');grip.type='button';grip.className='grip';grip.setAttribute('aria-label','Arrastar '+item.name);grip.innerHTML='<i></i><i></i><i></i><i></i><i></i><i></i>';
const idx=document.createElement('span');idx.className='idx';
const name=document.createElement('span');name.className='name';name.textContent=item.name;
const eye=document.createElement('button');eye.type='button';eye.className='eye'+(item.shown?' on':'');eye.textContent=item.shown?'Na home':'Oculta';eye.onclick=()=>toggleList(lists.indexOf(item));
li.append(grip,idx,name,eye);li.addEventListener('pointerdown',onDragDown);li.addEventListener('pointermove',onDragMove);li.addEventListener('pointerup',onDragUp);li.addEventListener('pointercancel',onDragUp);box.append(li)});paintRows()}
function paintRows(){let lead=false;const box=document.getElementById('lists');[...box.children].forEach((li,i)=>{li.querySelector('.idx').textContent=i+1;li.classList.toggle('off',!lists[i].shown);const on=!!lists[i].shown&&!lead;li.classList.toggle('lead',on);if(lists[i].shown)lead=true})}
function canDrag(t){return t&&t.closest&&(t.closest('.grip')||t.closest('.idx')||t.closest('.name'))}
function onDragDown(e){if(e.button||drag||!canDrag(e.target))return;const li=e.currentTarget;const box=li.parentNode;const rows=[...box.children];const from=rows.indexOf(li);const tops=rows.map(row=>row.getBoundingClientRect().top);box.classList.add('sorting');li.classList.add('dragging');drag={li,box,from,to:from,y:e.clientY,mids:rows.map((row,i)=>tops[i]+row.getBoundingClientRect().height/2),step:tops.length>1?tops[1]-tops[0]:li.getBoundingClientRect().height+8};try{li.setPointerCapture(e.pointerId)}catch(err){}e.preventDefault()}
function onDragMove(e){if(!drag||e.currentTarget!==drag.li)return;let to=0;for(let i=0;i<drag.mids.length;i++){if(i!==drag.from&&e.clientY>drag.mids[i])to++}drag.to=to;drag.li.style.transform='translateY('+(e.clientY-drag.y)+'px)';[...drag.box.children].forEach((row,i)=>{if(row===drag.li)return;let shift=0;if(to>drag.from&&i>drag.from&&i<=to)shift=-drag.step;if(to<drag.from&&i>=to&&i<drag.from)shift=drag.step;row.style.transform=shift?'translateY('+shift+'px)':''})}
function onDragUp(e){if(!drag||e.currentTarget!==drag.li)return;onDragMove(e);const from=drag.from,to=drag.to;drag.box.classList.remove('sorting');drag=null;if(to===from){drawLists();return}const item=lists.splice(from,1)[0];lists.splice(to,0,item);drawLists();post('/api/lists',{lists},'Ordem salva',false)}
function toggleList(i){if(lists[i].shown&&lists.filter(l=>l.shown).length==1){toast('Deixe ao menos uma lista');return}lists[i].shown=!lists[i].shown;drawLists();post('/api/lists',{lists},lists[i].shown?'Lista na home':'Lista oculta',false)}
async function post(u,b,msg,reload){let ok=false;try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});ok=r.ok;if(!ok){const t=await r.text();toast(t||'Não deu certo')}else toast(msg)}catch(e){toast('Painel não respondeu')}if(reload!==false)setTimeout(load,600);else if(!ok)setTimeout(load,400)}
let weatherPick=null,suggestTimer=0;
function saveCity(){const name=v('weather_city').trim();if(!name){toast('Digite a cidade');return}
if(weatherPick&&weatherPick.name===name){post('/api/weather',{name:weatherPick.name,latitude:weatherPick.latitude,longitude:weatherPick.longitude},'Local salvo');return}
post('/api/weather',{city:name},'Cidade salva')}
function useGeo(){if(!window.isSecureContext&&location.hostname!=='localhost'&&location.hostname!=='127.0.0.1'){toast('GPS do navegador exige HTTPS. Use a busca de cidade.');return}
if(!navigator.geolocation){toast('Sem geolocalização neste navegador');return}
toast('Pedindo localização…');
navigator.geolocation.getCurrentPosition(p=>{post('/api/weather',{latitude:p.coords.latitude,longitude:p.coords.longitude,name:'Minha localização'},'Local salvo')},err=>{const m=err&&err.code===1?'Permissão de localização negada':err&&err.code===2?'Localização indisponível':'Não foi possível obter a localização';toast(m+' — busque a cidade.')},{enableHighAccuracy:false,timeout:12000,maximumAge:60000})}
function drawSuggest(rows){const box=document.getElementById('weather_suggest');if(!box)return;box.innerHTML='';
if(!rows||!rows.length){box.hidden=true;return}box.hidden=false;
rows.forEach(row=>{const b=document.createElement('button');b.type='button';
const where=[row.admin1,row.country].filter(Boolean).join(' · ');
b.innerHTML='<b></b><span></span>';b.querySelector('b').textContent=row.name;b.querySelector('span').textContent=where;
b.onclick=()=>{weatherPick={name:row.name,latitude:row.latitude,longitude:row.longitude};document.getElementById('weather_city').value=row.name;clearTimeout(suggestTimer);box.innerHTML='';box.hidden=true;post('/api/weather',{name:row.name,latitude:row.latitude,longitude:row.longitude},'Local salvo')};box.appendChild(b)})}
async function suggestCity(){const q=v('weather_city').trim();weatherPick=null;if(q.length<2){drawSuggest([]);return}
try{const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?name='+encodeURIComponent(q)+'&count=5&language=pt&format=json');
const j=await r.json();drawSuggest(j.results||[])}catch(e){drawSuggest([])}}
const weatherInput=document.getElementById('weather_city');
if(weatherInput){weatherInput.addEventListener('input',()=>{clearTimeout(suggestTimer);suggestTimer=setTimeout(suggestCity,280)});weatherInput.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();saveCity()}})}
function osDark(){return !!(window.matchMedia&&matchMedia('(prefers-color-scheme: dark)').matches)}
function pushOsTheme(reload){if(!s.settings||+s.settings.appearance!==2)return;const d=osDark();
if(s.settings.os_dark===d&&s.settings.os_theme_known)return;
s.settings.os_dark=d;s.settings.os_theme_known=true;post('/api/settings',s.settings,'Tema do sistema',reload)}
const restOpts=[0,2,5,10,20,30],clockOpts=[0,1,2,5,10];
function nearestIdx(v,opts){let best=0;opts.forEach((o,i)=>{if(Math.abs(+o-+v)<Math.abs(+opts[best]-+v))best=i});return best}
function restLabel(v){return +v===0?'Nunca':(+v)+' min'}
function clockLabel(v){return +v===0?'Até tocar':(+v)+' min'}
function bindRange(id,labelId,opts,key,fmt){const el=document.getElementById(id),lab=document.getElementById(labelId);if(!el||!lab)return;
const idx=nearestIdx(s.settings[key],opts);el.value=idx;lab.textContent=fmt(opts[idx]);
el.oninput=()=>{lab.textContent=fmt(opts[+el.value])};
el.onchange=()=>{s.settings[key]=opts[+el.value];lab.textContent=fmt(opts[+el.value]);post('/api/settings',s.settings,'Salvo',false)}}
function drawAi(){const ai=s.aitokens||{};const note=document.getElementById('aitokens-updated');
if(note){if(!ai.ready)note.textContent='Última atualização: ainda não';
else if(ai.stale)note.textContent='Última atualização: desatualizado';
else if(+ai.age_s<60)note.textContent='Última atualização: há '+ai.age_s+' s';
else note.textContent='Última atualização: há '+Math.floor(+ai.age_s/60)+' min'}
document.querySelectorAll('[data-ai]').forEach(inp=>{const k=inp.dataset.ai;inp.checked=!!s.settings[k];
inp.onchange=()=>{s.settings[k]=!!inp.checked;post('/api/settings',s.settings,inp.checked?'Fonte na home':'Fonte oculta',false)}})}
function fmtBytes(n){n=+n||0;if(n>=1073741824)return(n/1073741824).toFixed(1).replace('.',',')+' GB';
if(n>=1048576)return(n/1048576).toFixed(1).replace('.',',')+' MB';
if(n>=1024)return(n/1024).toFixed(1).replace('.',',')+' KB';return n+' B'}
function pctUsed(free,total){total=+total||0;free=+free||0;if(!total)return 0;const used=Math.max(0,total-free);return Math.min(100,Math.round(used*100/total))}
function uptimeLabel(s){s=+s||0;if(s<60)return s+' s';if(s<3600)return Math.floor(s/60)+' min';return Math.floor(s/3600)+' h '+(Math.floor(s%3600/60))+' min'}
function resRow(title,main,extra,pct){const d=document.createElement('div');d.className='res-row';
d.innerHTML='<b></b><span></span>'+(extra?'<em></em>':'')+(pct!=null?'<div class="meter"><i></i></div>':'');
d.querySelector('b').textContent=title;d.querySelector('span').textContent=main;
const em=d.querySelector('em');if(em)em.textContent=extra;
const bar=d.querySelector('.meter i');if(bar)bar.style.width=Math.max(0,Math.min(100,+pct||0))+'%';
return d}
function drawResources(){const box=document.getElementById('res-grid');if(!box)return;box.innerHTML='';
const r=s.resources||{};
const heapFree=+r.heap_free||0,heapTotal=+r.heap_total||0;
box.appendChild(resRow('DRAM interna',fmtBytes(heapFree)+' livres de '+fmtBytes(heapTotal),
'Maior bloco '+fmtBytes(r.heap_largest)+' · mínimo histórico '+fmtBytes(r.heap_min),pctUsed(heapFree,heapTotal)));
const psFree=+r.psram_free||0,psTotal=+r.psram_total||0;
if(psTotal)box.appendChild(resRow('PSRAM',fmtBytes(psFree)+' livres de '+fmtBytes(psTotal),
'Maior bloco '+fmtBytes(r.psram_largest),pctUsed(psFree,psTotal)));
else box.appendChild(resRow('PSRAM','Indisponível',''));
const sketch=+r.flash_sketch||0,sketchFree=+r.flash_sketch_free||0,sketchCap=sketch+sketchFree;
box.appendChild(resRow('Firmware (flash)',fmtBytes(sketch)+' da imagem · '+fmtBytes(sketchFree)+' livres (OTA)',
'',sketchCap?pctUsed(sketchFree,sketchCap):0));
if(r.storage_ready&&+r.storage_total){
const kind=r.storage_kind||'Volume';
box.appendChild(resRow(kind,fmtBytes(r.storage_free)+' livres de '+fmtBytes(r.storage_total),
'',pctUsed(r.storage_free,r.storage_total)));
}else box.appendChild(resRow('Armazenamento','Nenhum volume montado',''));
box.appendChild(resRow('Ligado há',uptimeLabel(r.uptime_s),''))}
function chipValue(c,k){if(c.dataset.scope==='widget'){const w=(s.widget_settings&&s.widget_settings[c.dataset.widget])||{};
if(w[k]!=null)return +w[k];}return +((s.settings&&s.settings[k])||0)}
function draw(){document.querySelectorAll('.chips').forEach(c=>{const k=c.dataset.k;c.innerHTML='';
let opts=JSON.parse(c.dataset.o),best=0,cur=chipValue(c,k);opts.forEach((o,i)=>{if(Math.abs(+o[0]-cur)<Math.abs(+opts[best][0]-cur))best=i});
opts.forEach((o,i)=>{const b=document.createElement('button');b.textContent=o[1];if(i==best)b.className='on';
b.onclick=()=>{
if(c.dataset.scope==='widget'){if(!s.widget_settings)s.widget_settings={};if(!s.widget_settings[c.dataset.widget])s.widget_settings[c.dataset.widget]={};
s.widget_settings[c.dataset.widget][k]=o[0];if(s.settings)s.settings[k]=o[0];
draw();post('/api/widget_settings',{id:c.dataset.widget,key:k,value:o[0]},'Salvo');return}
s.settings[k]=o[0];
if(k==='appearance'&&+o[0]===2){s.settings.os_dark=osDark();s.settings.os_theme_known=true}
draw();post('/api/settings',s.settings,'Salvo')};c.appendChild(b)})});
const sound=document.getElementById('sound-tog');
if(sound){sound.checked=!!s.settings.sound;sound.onchange=()=>{s.settings.sound=!!sound.checked;post('/api/settings',s.settings,sound.checked?'Som ligado':'Som desligado',false)}}
bindRange('rest_range','rest_label',restOpts,'rest_minutes',restLabel);
bindRange('clock_range','clock_label',clockOpts,'clock_minutes',clockLabel);
drawAi()}
if(window.matchMedia){try{matchMedia('(prefers-color-scheme: dark)').addEventListener('change',()=>pushOsTheme(false))}catch(e){try{matchMedia('(prefers-color-scheme: dark)').addListener(()=>pushOsTheme(false))}catch(e2){}}}
async function load(){try{s=await (await fetch('/api/state')).json()}catch(e){document.getElementById('who').textContent='painel não respondeu, tentando de novo…';setTimeout(load,2000);return}
const online=!!s.info.online;
document.getElementById('who').textContent=online?`${s.info.ssid} · ${s.info.ip}`:'sem Wi-Fi';
document.getElementById('host').textContent=s.info.host||'';
const link=document.getElementById('link');link.className=online?'status on':'status';
link.querySelector('span').textContent=online?`Conectado em ${s.info.ssid}`:'Desconectado';
lists=(s.lists||[]).map(item=>typeof item==='string'?{name:item,shown:true}:{name:item.name,shown:!!item.shown});
drawLists();
widgets=(s.widgets||[]).map(item=>({id:item.id,title:item.title||item.id,shown:!!item.shown}));
drawWidgets();
document.body.classList.toggle('wait-token',!s.token_set);
document.getElementById('need').style.display=s.token_set?'none':'block';
if(!s.token_set&&document.body.dataset.page==='hub')showPage('todoist');
document.getElementById('tok').textContent=s.token_set?'Já configurado. Só preencha para trocar.':'Cole aqui o token copiado em Desenvolvedor.';
if(!document.getElementById('ssid').value)document.getElementById('ssid').value=s.info.ssid||'';
const wc=document.getElementById('weather_city');if(wc&&!wc.value)wc.value=(s.weather&&s.weather.city)||'';
const wn=document.getElementById('weather-note');if(wn)wn.textContent=s.weather&&s.weather.ready?'Previsão fresca no painel.':(s.weather&&s.weather.city?'Aguardando ou desatualizada no painel.':'Defina a cidade para ver o clima na home.');
const ag=s.agent||{};const as=document.getElementById('agent-status');
const hint=document.getElementById('agent-status-hint');
if(as){
if(!ag.ready){as.textContent='Aguardando agente…';as.className='off';
if(hint)hint.textContent='Rode o comando de instalar (ou confira se o LaunchAgent está ativo). O status muda sozinho quando o Mac falar com o painel.'}
else if(ag.ok){as.textContent='OK — comunicando';as.className='ok';
if(hint)hint.textContent='Agente no Mac respondeu via USB. Atualiza a cada poucos segundos.'}
else{as.textContent=ag.detail||'Falha';as.className='warn';
if(hint)hint.textContent=ag.detail==='sem sinal recente'
?'O agente parou de responder. Confira USB e se o processo tessera-agent está rodando.'
:'Handshake recebido, mas o agente reportou falha.'}
}
const ap=document.getElementById('agent-pack-note');
const boxUp=document.getElementById('agent-upload-box');
const boxIn=document.getElementById('agent-install-box');
if(ap){if(s.agent_pack){ap.textContent='Pronto';ap.className='ok'}else{ap.textContent='Ausente';ap.className='warn'}}
if(boxUp&&boxIn){const forceUp=document.body.dataset.agentForceUpload==='1';
if(s.agent_pack&&!forceUp){boxUp.style.display='none';boxIn.style.display='grid'}
else{boxUp.style.display='grid';boxIn.style.display=s.agent_pack?'grid':'none'}}
const cmdEl=document.getElementById('agent-install-cmd');
if(cmdEl){cmdEl.textContent=`curl -fsSL ${location.origin}/agent/install.command | bash`}
draw();drawResources();pushOsTheme(false)}
async function putFile(url,file){const r=await fetch(url,{method:'PUT',headers:{'Content-Type':'application/octet-stream'},body:file});
if(!r.ok){const t=await r.text();throw new Error(t||('HTTP '+r.status))}return r}
async function uploadAgentPack(){const tgz=document.getElementById('agent-file-tgz').files[0];
if(!tgz){toast('Escolha o agent.tgz');return}
toast('Enviando…');
try{await putFile('/agent/agent.tgz',tgz);document.body.dataset.agentForceUpload='';
toast('Pacote pronto — copie o comando de instalar');setTimeout(load,400)}catch(e){toast(e&&e.message?e.message:'Falha no upload')}}
async function copyInstallCmd(){
const text=`curl -fsSL ${location.origin}/agent/install.command | bash`;
try{await navigator.clipboard.writeText(text);toast('Comando copiado — cole no Terminal e olhe o Status')}
catch(e){toast('Selecione o comando e copie (⌘C)')}
}
const up=document.getElementById('agent-upload');if(up)up.onclick=uploadAgentPack;
const re=document.getElementById('agent-reupload');if(re)re.onclick=()=>{document.body.dataset.agentForceUpload='1';load()};
const cp=document.getElementById('agent-copy-cmd');if(cp)cp.onclick=copyInstallCmd;
load();setInterval(load,2500)
</script></body></html>)HTML";

}  // namespace web_portal
