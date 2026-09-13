"use strict";
let launcherItems=[],launcherDraft=null,launcherSection="root";
async function loadRemoteLauncher() {
  try {
    const result=await api("/api/launcher");launcherItems=result.items;$("#configureLauncher").disabled=false;
    $("#launcherSource").textContent=result.source==="pi"?"Configuration read from the Pi.":"Pi unavailable: showing the laptop configuration.";
    renderRemoteLauncher();
  } catch(error){setAction(error.message,true);}
}
function renderRemoteLauncher() {
  $("#remoteMenuTitle").textContent=launcherSection==="root"?"Two Forty":launcherItems.find(e=>e.id==="settings" && e.section==="root")?.label || "Settings";
  $("#launcherItems").innerHTML=launcherItems.filter(e=>e.section===launcherSection && e.visible).map(e=>`<button class="remote-item" data-menu-id="${e.id}"><span>${escapeHtml(e.label)}</span><span aria-hidden="true">${e.id==="settings"?"›":"↗"}</span></button>`).join("");
  if(launcherSection==="settings")$("#launcherItems").insertAdjacentHTML("beforeend",'<button class="remote-item" data-menu-id="back"><span>Back</span><span aria-hidden="true">‹</span></button>');
  document.querySelectorAll("[data-menu-id]").forEach(button=>button.onclick=()=>{
    const id=button.dataset.menuId;
    if(launcherSection==="settings" && id==="back"){launcherSection="root";renderRemoteLauncher();return;}
    if(launcherSection==="root" && id==="settings") {launcherSection="settings";renderRemoteLauncher();return;}
    if(launcherSection==="root" && id==="power" && !confirm("Power down the Pi? You will need to power it back on to reconnect."))return;
    const action=launcherSection==="settings"?`settings ${id}`:id==="power"?"poweroff":"launch";
    act(button,"Opening",async()=>{
      await api("/api/control",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({action,game:id})});
      await refreshStatus();
    });
  });
}
function readLauncherDraft() {
  document.querySelectorAll("[data-launcher-label]").forEach(input=>launcherDraft[Number(input.dataset.launcherLabel)].label=input.value);
  document.querySelectorAll("[data-launcher-visible]").forEach(input=>launcherDraft[Number(input.dataset.launcherVisible)].visible=input.checked);
}
function renderLauncherEditor() {
  $("#launcherFields").innerHTML=["root","settings"].map(section=>{
    const entries=launcherDraft.map((e,i)=>({...e,index:i})).filter(e=>e.section===section);
    return `<fieldset><legend>${section==="root"?"Main menu":"Settings menu"}</legend>${entries.map((e,n)=>`<div class="launcher-edit-row"><label>Name <input data-launcher-label="${e.index}" value="${escapeHtml(e.label)}" maxlength="24"></label><label class="visibility-label"><input type="checkbox" data-launcher-visible="${e.index}" ${e.visible?"checked":""}> Visible</label><div class="button-row"><button class="button compact" data-move="${e.index}" data-direction="-1" ${n===0?"disabled":""} aria-label="Move ${escapeHtml(e.label)} up">Up</button><button class="button compact" data-move="${e.index}" data-direction="1" ${n===entries.length-1?"disabled":""} aria-label="Move ${escapeHtml(e.label)} down">Down</button></div></div>`).join("")}</fieldset>`;
  }).join("");
  document.querySelectorAll("[data-move]").forEach(button=>button.onclick=()=>{
    readLauncherDraft();const index=Number(button.dataset.move),direction=Number(button.dataset.direction);
    const indices=launcherDraft.map((e,i)=>e.section===launcherDraft[index].section?i:-1).filter(i=>i>=0);
    const other=indices[indices.indexOf(index)+direction];
    [launcherDraft[index],launcherDraft[other]]=[launcherDraft[other],launcherDraft[index]];renderLauncherEditor();
  });
}
$("#configureLauncher").onclick=()=>{
  launcherDraft=launcherItems.map(e=>({...e}));renderLauncherEditor();
  $("#launcherEditor").classList.remove("hidden");$("#launcherSaveStatus").textContent="Changes are a draft until applied.";
  $("#launcherEditor").scrollIntoView({behavior:"smooth",block:"start"});
};
$("#cancelLauncher").onclick=()=>{$("#launcherEditor").classList.add("hidden");launcherDraft=null;};
$("#saveLauncher").onclick=async()=>{
  readLauncherDraft();const button=$("#saveLauncher");button.disabled=true;$("#launcherSaveStatus").textContent="Applying…";
  $("#launcherFields").inert=true;$("#cancelLauncher").disabled=true;$("#configureLauncher").disabled=true;
  try {
    const result=await api("/api/launcher",{method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify({items:launcherDraft})});
    launcherItems=result.items;renderRemoteLauncher();$("#launcherEditor").classList.add("hidden");
    $("#launcherSource").textContent="Configuration saved on the laptop and Pi.";setAction("Launcher updated on the dashboard and CRT.");
  } catch(error){$("#launcherSaveStatus").textContent=error.message;}
  finally {button.disabled=false;$("#launcherFields").inert=false;$("#cancelLauncher").disabled=false;$("#configureLauncher").disabled=false;}
};
loadRemoteLauncher();
