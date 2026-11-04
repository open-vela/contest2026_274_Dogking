import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {FileBlob,PresentationFile} from 'file:///C:/Users/Lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';
const build=process.env.A733_DEFENSE_BUILD_DIR??path.dirname(fileURLToPath(import.meta.url));
const revision=process.argv[2]??'r5';
const source=path.resolve(build,`../第一版_20261031/Vela伴星_决赛答辩_第一版_${revision}_字体兼容修复.pptx`);
const dest=path.join(build,revision,'final-render');await fs.mkdir(dest);
const p=await PresentationFile.importPptx(await FileBlob.load(source));
for(let i=0;i<p.slides.items.length;i++){
  const b=await p.export({slide:p.slides.items[i],format:'png',scale:0.5});
  await fs.writeFile(path.join(dest,`slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await b.arrayBuffer()));
  console.log('RENDER',i+1);
}
console.log('RENDERED_ALL_24');
