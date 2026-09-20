// Convert the submission technical report (Markdown) to DOCX.
// Usage: node convert.mjs <input.md> <output.docx>
import fs from 'node:fs/promises';
import markdownDocx, { Packer } from 'markdown-docx';

const [, , inPath, outPath] = process.argv;
if (!inPath || !outPath) {
  console.error('usage: node convert.mjs <input.md> <output.docx>');
  process.exit(2);
}

const markdown = await fs.readFile(inPath, 'utf-8');
console.log(`input : ${inPath} (${markdown.length} chars)`);

const doc = await markdownDocx(markdown);
const buffer = await Packer.toBuffer(doc);
await fs.writeFile(outPath, buffer);

const st = await fs.stat(outPath);
console.log(`output: ${outPath} (${st.size} bytes)`);
