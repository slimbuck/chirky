"use strict";
// Keep the archival SVG aligned with the launcher's custom pixel glyphs.
const fs=require("node:fs");
const path=require("node:path");
const root=path.resolve(__dirname,"..");
const source=fs.readFileSync(path.join(root,"include/launcher_wordmark.h"),"utf8");
const glyphs=[...source.matchAll(/\{((?:0x[0-9a-f]+,?)+)\},?\s*\/\* ([A-Z]) \*\//g)]
  .map(match=>({letter:match[2],rows:match[1].split(",").map(value=>parseInt(value,16))}));
if(glyphs.map(g=>g.letter).join("")!=="CHIRKYBOX" || glyphs.some(g=>g.rows.length!==9))
  throw new Error("Unexpected launcher glyph format");
function letterPath(glyph,index) {
  const x=index<6?8+index*24:164+(index-6)*24;
  const segments=[];
  glyph.rows.forEach((bits,row)=>{
    for(let col=0;col<7;) {
      if(!(bits&(1<<(6-col)))) {col++;continue;}
      let end=col+1;
      while(end<7 && (bits&(1<<(6-end))))end++;
      segments.push(`M${x+col*3} ${14+row*3}h${(end-col)*3}v3h-${(end-col)*3}z`);
      col=end;
    }
  });
  return segments.join("");
}
const paths=glyphs.map(letterPath);
const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="744" height="168" viewBox="0 0 248 56" role="img" aria-labelledby="title description" shape-rendering="crispEdges">
  <title id="title">Chirky Box — maker's badge</title>
  <desc id="description">Cream pixel lettering with BOX inside an amber frame, matching the launcher's native pixel logo. Transparent surrounding canvas; no fonts or linked images.</desc>
  <g id="badge">
    <rect x="159" y="9" width="81" height="39" fill="#082a2c"/>
    <rect x="158" y="8" width="81" height="39" fill="#f4b845"/>
    <rect x="160" y="10" width="77" height="35" fill="#051117"/>
  </g>
  <g id="letter-shadows" fill="#082a2c" transform="translate(1 1)">
${paths.map(d=>`    <path d="${d}"/>`).join("\n")}
  </g>
  <g id="chirky" fill="#f4e9ca">
${paths.slice(0,6).map((d,i)=>`    <path id="letter-${glyphs[i].letter.toLowerCase()}" d="${d}"/>`).join("\n")}
  </g>
  <g id="box" fill="#f4b845">
${paths.slice(6).map((d,i)=>`    <path id="letter-${glyphs[i+6].letter.toLowerCase()}" d="${d}"/>`).join("\n")}
  </g>
</svg>
`;
const output=path.join(root,"assets/launcher/chirky-box.svg");
fs.writeFileSync(output,svg);
console.log(output);
