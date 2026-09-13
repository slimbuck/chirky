"use strict";

function decodePpm(bytes) {
  let position=0;
  const whitespace=value=>[9,10,11,12,13,32].includes(value);
  const token=()=>{
    while(position<bytes.length) {
      if(bytes[position]===35)while(position<bytes.length && bytes[position++]!==10) {}
      else if(whitespace(bytes[position]))position++;
      else break;
    }
    const start=position;
    while(position<bytes.length && !whitespace(bytes[position]) && bytes[position]!==35)position++;
    return new TextDecoder().decode(bytes.subarray(start,position));
  };
  if(token()!=="P6")throw new Error("Unsupported snapshot format");
  const width=Number(token()),height=Number(token()),maximum=Number(token());
  if(!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width<=0 || height<=0)
    throw new Error("Invalid snapshot dimensions");
  if(maximum!==255)throw new Error("Unsupported snapshot depth");
  if(!whitespace(bytes[position]))throw new Error("Invalid snapshot header");
  // Exactly one separator precedes binary pixels. Dark pixels are data, too.
  position++;
  const length=width*height*3;
  if(!Number.isSafeInteger(length) || bytes.length-position!==length)
    throw new Error("Incomplete or invalid snapshot pixels");
  return {width,height,pixels:bytes.subarray(position)};
}

if(typeof module!=="undefined")module.exports={decodePpm};
