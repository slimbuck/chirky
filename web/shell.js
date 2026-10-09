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
  // Measure the available grid afresh before wrapping the desktop shell around
  // the fitted screen. Resetting avoids feedback when resizing or rotating.
  player.style.removeProperty("width");player.style.removeProperty("height");
  $("header").style.removeProperty("width");
  // Fit whole physical pixels, including fractional browser/OS pixel densities.
  // Only downscale when even a native-size physical frame cannot fit.
  const bounds=slot.getBoundingClientRect();
  const density=window.devicePixelRatio || 1;
  const fit=Math.min(Math.max(1,bounds.width-2)/320,Math.max(1,bounds.height-2)/240)*density;
  const scale=(fit>=1?Math.floor(fit+1e-6):fit)/density;
  canvas.style.width="320px";canvas.style.height="240px";
  canvas.style.transformOrigin="top left";canvas.style.transform=`scale(${scale})`;
  display.style.width=`${320*scale+2}px`;display.style.height=`${240*scale+2}px`;
  if(!touchVisible && !fullscreen){
    const style=getComputedStyle(player),px=name=>parseFloat(style[name]) || 0;
    const width=320*scale+2+px("paddingLeft")+px("paddingRight")+px("borderLeftWidth")+px("borderRightWidth");
    player.style.width=`${width}px`;
    // The keyboard guide can wrap after the shell narrows. Measure it at that
    // final width so the screen and all hints retain their own space.
    player.style.height=`${240*scale+2+$(".help").getBoundingClientRect().height+px("rowGap")+px("paddingTop")+px("paddingBottom")+px("borderTopWidth")+px("borderBottomWidth")}px`;
    $("header").style.width=`${width}px`;
  }
  // Centre the shell first, then align its native canvas with physical pixels.
  display.style.position="relative";display.style.left="0px";display.style.top="0px";
  const rect=canvas.getBoundingClientRect();
  display.style.left=`${Math.round(rect.left*density)/density-rect.left}px`;
  display.style.top=`${Math.round(rect.top*density)/density-rect.top}px`;
  // Layout positions round to CSS subpixels. Bias within the intended physical
  // pixel to avoid nearest-neighbour ties on fractional Android pixel densities.
  const aligned=canvas.getBoundingClientRect();
  canvas.style.transform=`translate(${(Math.round(aligned.left*density)-1/3)/density-aligned.left}px,${(Math.round(aligned.top*density)-1/3)/density-aligned.top}px) scale(${scale})`;
}
document.addEventListener("fullscreenchange",resizePlayer);
document.addEventListener("webkitfullscreenchange",resizePlayer);
window.addEventListener("resize",resizePlayer);
window.visualViewport?.addEventListener("resize",resizePlayer);
if(typeof matchMedia==="function")matchMedia("(any-pointer: coarse)").addEventListener("change",resizePlayer);
function watchPixelDensity(){
  if(typeof matchMedia!=="function")return;
  matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`).addEventListener("change",()=>{resizePlayer();watchPixelDensity();},{once:true});
}
watchPixelDensity();
resizePlayer();
window.ChirkyShell={playerKeysConflict,inputNames,defaultKeys,settingsKey,validKeys,keyName,renderInputSettings,initialSettings,resizePlayer};
})();
