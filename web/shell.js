// Inlined into the page by web-assets.js so its first paint uses the final layout.
// Keep initial layout and later resize/fullscreen changes on the same path.
(() => {
const $=selector=>document.querySelector(selector);
const canvas=$("#screen");
const inputNames=["Left","Right","Up","Down","Primary","Secondary","Start","Menu"];
const defaultKeys=["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","KeyN","KeyM","Enter","Escape"];
const settingsKey="chirky.inputs.v1";
function validKeys(value){return Array.isArray(value) && value.length===8 && new Set(value).size===8 && value.every(code=>typeof code==="string" && /^(Arrow(Left|Right|Up|Down)|Key[A-Z]|Digit[0-9]|Numpad[0-9]|Enter|Escape|Space|Shift(Left|Right)|Control(Left|Right)|Alt(Left|Right)|Backspace|Tab|Comma|Period|Slash|Semicolon|Quote|BracketLeft|BracketRight|Backslash|Minus|Equal|Backquote)$/.test(code));}
const defaultPlayer2Keys=["KeyA","KeyD","KeyW","KeyS","KeyF","KeyG","Enter","Escape"];
function playerKeysConflict(a,b){
  return a.some((key,i)=>b.some((other,j)=>key===other && !(i===j && i>=6)));
}
function loadInputSettings(){
  try{
    const saved=JSON.parse(localStorage.getItem(settingsKey));
    if([1,2].includes(saved?.version) && validKeys(saved.keys) && ["auto","show","hide"].includes(saved.touch)){
      const oldDefault=["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","KeyX","KeyZ","Enter","Escape"];
      const keys=saved.version===1 && saved.keys.every((key,i)=>key===oldDefault[i])?[...defaultKeys]:saved.keys;
      const p2Keys=saved.version===2 && validKeys(saved.p2Keys)?saved.p2Keys:[...defaultPlayer2Keys];
      if(!playerKeysConflict(keys,p2Keys))return {...saved,version:2,keys,p2Keys};
      return {version:2,keys:[...defaultKeys],p2Keys:[...defaultPlayer2Keys],touch:saved.touch};
    }
  }catch{}
  return {version:2,keys:[...defaultKeys],p2Keys:[...defaultPlayer2Keys],touch:"auto"};
}
function keyName(code){return ({ArrowLeft:"←",ArrowRight:"→",ArrowUp:"↑",ArrowDown:"↓"})[code] || code.replace(/^Key|^Digit/,"").replace("Escape","Esc");}
function keycapName(code){
  const names={ArrowLeft:"←",ArrowRight:"→",ArrowUp:"↑",ArrowDown:"↓",Enter:"Enter ↵",Escape:"Esc",
    ShiftLeft:"Shift L",ShiftRight:"Shift R",ControlLeft:"Ctrl L",ControlRight:"Ctrl R",AltLeft:"Alt L",AltRight:"Alt R",
    Backspace:"Backspace",Comma:",",Period:".",Slash:"/",Semicolon:";",Quote:"'",BracketLeft:"[",BracketRight:"]",Backslash:"\\",Minus:"−",Equal:"=",Backquote:"`"};
  return names[code] || keyName(code).replace(/^Numpad/,"Num ");
}
function renderInputSettings(inputSettings){
  $("#player").setAttribute("data-touch",inputSettings.touch);
  inputSettings.keys.forEach((code,index)=>{
    const key=$("#key-help-"+index);key.textContent=keycapName(code);
    key.setAttribute("aria-label",`${inputNames[index]}: ${keyName(code)}`);
  });
  resizePlayer();
}
const initialSettings=loadInputSettings();
renderInputSettings(initialSettings);
function resizePlayer(){
  if(typeof innerWidth!=="number")return;
  const fullscreen=!!(document.fullscreenElement || document.webkitFullscreenElement),player=$("#player"),display=$("#display"),slot=$("#screen-slot");
  player.classList.toggle("is-fullscreen",fullscreen);
  const touchVisible=getComputedStyle($(".touch")).display!=="none";
  const sideControls=touchVisible && innerWidth>innerHeight;
  const bottomControls=touchVisible && !sideControls;
  player.classList.toggle("side-controls",sideControls);
  player.classList.toggle("bottom-controls",bottomControls);
  document.body.classList.toggle("controller-wide",sideControls && !fullscreen);
  document.body.classList.toggle("controller-portrait",bottomControls && !fullscreen);
  player.style.setProperty("--player-height",`${window.visualViewport?.height || innerHeight}px`);
  // The grid reserves room for controls/help. Fill its remaining slot at 4:3,
  // including fractional scales, without changing the native framebuffer.
  const bounds=slot.getBoundingClientRect();
  const scale=Math.min(Math.max(1,bounds.width-2)/320,Math.max(1,bounds.height-2)/240);
  canvas.style.width="320px";canvas.style.height="240px";
  canvas.style.transformOrigin="top left";canvas.style.transform=`scale(${scale})`;
  display.style.width=`${320*scale+2}px`;display.style.height=`${240*scale+2}px`;
}
document.addEventListener("fullscreenchange",resizePlayer);
document.addEventListener("webkitfullscreenchange",resizePlayer);
window.addEventListener("resize",resizePlayer);
window.visualViewport?.addEventListener("resize",resizePlayer);
if(typeof matchMedia==="function")matchMedia("(any-pointer: coarse)").addEventListener("change",resizePlayer);
resizePlayer();
window.ChirkyShell={playerKeysConflict,inputNames,defaultKeys,settingsKey,validKeys,keyName,renderInputSettings,initialSettings,resizePlayer};
})();
