import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {FileBlob,PresentationFile} from 'file:///C:/Users/Lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';

// A focused font repair. Author the font edits with Artifact Tool, then transplant
// font declarations plus explicitly approved fit corrections into the original
// OPC package. The editable chart workbook, photos and speaker notes stay intact.
const runtime='C:/Users/Lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies';
const require=createRequire(path.join(runtime,'node/node_modules/@oai/artifact-tool/package.json'));
const JSZip=require('jszip');
const build=process.env.A733_DEFENSE_BUILD_DIR??path.dirname(fileURLToPath(import.meta.url));
const workspace=path.resolve(build,'..');
const revision=process.argv[2]??'r5';
if(!['r5','r6','r7'].includes(revision))throw Error('unsupported revision');
const privateDir=path.join(build,revision);
const source=path.join(workspace,'第一版_20261031/Vela伴星_决赛答辩_第一版_r4.pptx');
const sourceHash='6d7ba078aa610624b3ab10592717ba0b9ec602ac9e748eaa4a871ed6a9736c0e';
const sha=b=>createHash('sha256').update(b).digest('hex');
const bytes=await fs.readFile(source);
if(sha(bytes)!==sourceHash)throw Error('r4 changed; preserve user changes and stop');
await fs.mkdir(privateDir);
const skill='C:/Users/Lenovo/.codex/plugins/cache/openai-primary-runtime/presentations/26.1020.11618/skills/presentations';
const {applyPresentationChartFont}=await import(`file:///${skill}/container_tools/artifact_tool_utils.mjs`);
const deck=await PresentationFile.importPptx(await FileBlob.load(source));
const before=await deck.inspect({select:'$.slides[*]',include:['id','index','elements.id','elements.kind','elements.text','elements.position','notes.text']});
if(before.records.length!==24)throw Error('unexpected source slide count');
deck.theme.defaultFont='Arial';
const touched=[];
for(const slide of before.records){
  for(const element of slide.elements){
    const target=deck.resolve(element.id);
    if(element.kind==='textbox'&&element.text){
      if(revision!=='r5'&&(element.text.startsWith('CPU LLM')||element.text.startsWith('复用 BSP'))){
        target.text=element.text.replace('，','，\n');
      }
      target.text.style={typeface:'Microsoft YaHei'};touched.push(element.id);
    }
    if(element.kind==='table'){
      for(let r=0;r<6;r++)for(let c=0;c<3;c++)target.getCell(r,c).text.style={typeface:'Microsoft YaHei'};
      touched.push(element.id);
    }
    if(element.kind==='chart'){applyPresentationChartFont(target,{fontFamily:'Microsoft YaHei'});touched.push(element.id);}
    const b=element.position;
    if(revision==='r7'&&slide.index===5&&!element.text&&b&&b.width>500&&b.width<510&&b.height===200){
      target.position={...b,height:240};
    }
  }
}
const after=await deck.inspect({select:'$.slides[*]',include:['index','elements.kind','elements.text','elements.position','notes.text']});
await fs.writeFile(path.join(privateDir,'api-font-edit.json'),JSON.stringify({before:before.records,after:after.records,touched},null,2));
await (await PresentationFile.exportPptx(deck)).save(path.join(privateDir,'api-font-edit.pptx'));
console.log('ARTIFACT_FONT_EDIT',touched.length);

const zip=await JSZip.loadAsync(bytes);
const changed=[];const removed=[];const softBreaks=[];let declarations=0;
function fontTag(tag,family,charset){
  let s=tag.replace(/\s+typeface="[^"]*"/g,'').replace(/\s+charset="[^"]*"/g,'');
  return s.replace(/\s*\/?>$/,end=>` typeface="${family}"${charset==null?'':` charset="${charset}"`}${end}`);
}
for(const name of Object.keys(zip.files)){
  if(name.startsWith('ppt/fonts/')&&!zip.files[name].dir){zip.remove(name);removed.push(name);continue;}
  if(!name.endsWith('.xml')&&!name.endsWith('.rels'))continue;
  const old=await zip.file(name)?.async('string');if(old==null)continue;
  let xml=old;
  if(name.startsWith('ppt/')){
    xml=xml.replace(/<a:(latin|cs|sym|buFont)\b[^>]*\/?\s*>/g,tag=>{declarations++;return fontTag(tag,'Arial',0);});
    xml=xml.replace(/<a:ea\b[^>]*\/?\s*>/g,tag=>{declarations++;return fontTag(tag,'Microsoft YaHei',134);});
    xml=xml.replace(/<a:font\b[^>]*\/?\s*>/g,tag=>{declarations++;return fontTag(tag,/script="(?:Hans|Hant|Jpan|Hang)"/.test(tag)?'Microsoft YaHei':'Arial',null);});
  }
  if(revision!=='r5'&&/^ppt\/slides\/slide(?:6|16)\.xml$/.test(name)){
    const prefix=name.endsWith('/slide6.xml')?'CPU LLM':'复用 BSP';
    let hits=0;
    xml=xml.replace(/<a:r>(<a:rPr\b[\s\S]*?<\/a:rPr>)(<a:t\b[^>]*>)([\s\S]*?)<\/a:t><\/a:r>/g,(run,pr,tag,text)=>{
      if(!text.startsWith(prefix))return run;
      const split=text.indexOf('，')+1;if(split<1)throw Error('missing soft-break anchor');
      hits++;return `<a:r>${pr}${tag}${text.slice(0,split)}</a:t></a:r><a:br/><a:r>${pr}${tag}${text.slice(split)}</a:t></a:r>`;
    });
    if(hits!==1)throw Error('soft-break anchor is not unique: '+name);
    softBreaks.push(name);
  }
  if(revision==='r7'&&name==='ppt/slides/slide6.xml'){
    let frames=0;
    xml=xml.replace(/<p:sp>[\s\S]*?<\/p:sp>/g,shape=>{
      const id=/<p:cNvPr\b[^>]*\bid="(\d+)"/.exec(shape)?.[1];
      if(!['13','17','21','25'].includes(id))return shape;
      if(!/<a:ext\b[^>]*\bcx="4826000"[^>]*\bcy="1905000"/.test(shape))throw Error('architecture frame geometry changed');
      frames++;return shape.replace(/(<a:ext\b[^>]*\bcy=")1905000("[^>]*>)/,'$12286000$2');
    });
    if(frames!==4)throw Error('architecture frame count mismatch');
  }
  if(name==='ppt/presentation.xml'){
    xml=xml.replace(/<p:embeddedFontLst\b[\s\S]*?<\/p:embeddedFontLst>/g,'');
    xml=xml.replace(/<p:presentation\b[^>]*>/,tag=>tag.replace(/\s+(?:embedTrueTypeFonts|saveSubsetFonts)="[^"]*"/g,'').replace(/>$/,' embedTrueTypeFonts="0" saveSubsetFonts="0">'));
  }
  if(name.endsWith('.rels'))xml=xml.replace(/<Relationship\b[^>]*\bType="[^"]*\/font"[^>]*\/>/g,'');
  if(name==='[Content_Types].xml')xml=xml.replace(/<Override\b[^>]*\bPartName="\/ppt\/fonts\/[^" ]+"[^>]*\/>/g,'');
  if(xml!==old){zip.file(name,xml);changed.push(name);}
}
if(declarations<100||removed.length!==2)throw Error('unexpected font source inventory');
const candidate=path.join(privateDir,'candidate.pptx');
const outputBytes=await zip.generateAsync({type:'nodebuffer',compression:'DEFLATE',compressionOptions:{level:6}});
await fs.writeFile(candidate,outputBytes,{flag:'wx'});
if(sha(await fs.readFile(source))!==sourceHash)throw Error('source was modified');
await fs.writeFile(path.join(privateDir,'font-repair.json'),JSON.stringify({source,sourceSha256:sourceHash,candidate,
  candidateSha256:sha(outputBytes),slides:24,latin:'Arial',eastAsian:'Microsoft YaHei',
  editedViaArtifactTool:true,surgicalOriginalPackage:true,changedParts:changed,removedFontParts:removed,
  fontDeclarations:declarations,softBreaks,architectureFramesGrown:revision==='r7'?['13','17','21','25']:[],sourcePreserved:true,nativePowerPointTested:false},null,2));
console.log(JSON.stringify({candidate,fontDeclarations:declarations,changedParts:changed.length,removedFontParts:removed}));
