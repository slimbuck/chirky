"use strict";

function renderCampaign(game) {
  const levels=game.editors.filter(e=>e.catalogKind==="level");
  const panel=document.createElement("section");
  panel.className="panel campaign-panel";
  panel.innerHTML=`<div class="panel-head"><h2>Levels</h2><span class="pill">${levels.length} levels</span></div>
    <p class="help-text">Played in this order. Changes save on this laptop; use Save and play in the editor or Install project on Pi to send them to the console.</p>
    <ol class="campaign-list">${levels.map((level,i)=>`<li data-level="${level.id}"><span class="campaign-number">${i+1}</span><label>Level name<input maxlength="80" value="${escapeHtml(level.name)}" aria-label="Level ${i+1} name"></label><div class="button-row"><button class="button compact" data-action="rename">Save name</button><button class="button compact" data-action="up" ${i===0?"disabled":""} aria-label="Move ${escapeHtml(level.name)} up">Move up</button><button class="button compact" data-action="down" ${i===levels.length-1?"disabled":""} aria-label="Move ${escapeHtml(level.name)} down">Move down</button><button class="button compact" data-action="edit">Edit level</button></div></li>`).join("")}</ol>
    <form class="campaign-add"><h3>Add level</h3><label>Level name<input name="name" required maxlength="80" placeholder="e.g. Furnace shaft"></label><label>Start with<select name="source"><option value="blank">Empty layout</option>${levels.map(e=>`<option value="${e.id}">Copy of ${escapeHtml(e.name)}</option>`).join("")}</select></label><button class="button primary" type="submit">Add level</button><p class="help-text">Empty layouts include a floor, a start point, and an exit, ready to edit.</p></form>`;
  $("#gameTools").append(panel);
  const firstLevelButton=$("#gameTools [data-level-editor]");
  if(firstLevelButton && levels.some(e=>e.id===firstLevelButton.dataset.levelEditor)) firstLevelButton.onclick=()=>panel.scrollIntoView({behavior:"smooth"});
  const request=(url,method,body)=>api(url,{method,headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
  const base=`/api/games/${game.id}`;
  function ensureSaved() {
    if(levelState?.gameId===game.id && levelText()!==levelState.savedText) throw new Error("Save your current level edits before changing the level list.");
  }
  async function refresh() {
    const opened=levelState?.gameId===game.id && !$("#levelEditorPanel").classList.contains("hidden") ? levelState.editorId : null;
    await loadGames();
    if(opened) await openLevelEditor(game.id,opened);
  }
  panel.querySelectorAll("[data-action]").forEach(button=>button.onclick=()=>act(button,button.dataset.action==="edit"?"Opening":"Saving",async()=>{
    const row=button.closest("[data-level]"), id=row.dataset.level;
    if(button.dataset.action==="edit") return openLevelEditor(game.id,id);
    ensureSaved();
    if(button.dataset.action==="rename") {
      const current=await api(`${base}/editors/${id}`);
      await request(`${base}/editors/${id}/name`,"PUT",{name:row.querySelector("input").value,hash:current.hash});
    } else {
      const ids=levels.map(e=>e.catalogId), index=levels.findIndex(e=>e.id===id), next=index+(button.dataset.action==="up"?-1:1);
      [ids[index],ids[next]]=[ids[next],ids[index]];
      await request(`${base}/campaign`,"PUT",{ids});
    }
    await refresh();
    setAction("Level list saved locally.");
  }));
  panel.querySelector("form").onsubmit=event=>{
    event.preventDefault();
    const form=event.currentTarget;
    act(form.querySelector("button"),"Adding",async()=>{
      ensureSaved();
      const source=form.elements.source.value;
      const result=await request(`${base}/editors`,"POST",{id:`level-${crypto.randomUUID()}`,name:form.elements.name.value,source:source==="blank"?levels[0].id:source,blank:source==="blank"});
      await loadGames();
      await openLevelEditor(game.id,result.editorId);
      setAction("Level added at the end of the campaign. Ready to edit.");
    });
  };
}
