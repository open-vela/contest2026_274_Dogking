import path from 'node:path';
import {fileURLToPath} from 'node:url';
const build=process.env.A733_DEFENSE_BUILD_DIR??path.dirname(fileURLToPath(import.meta.url));
const workspace=path.resolve(build,'..');
const skill='C:/Users/Lenovo/.codex/plugins/cache/openai-primary-runtime/presentations/26.1020.11618/skills/presentations';
const {finalizePresentation}=await import(`file:///${skill}/container_tools/artifact_tool_utils.mjs`);
const tableSlides=[8,15,17,21];
const revision=process.argv[2]??'r5';
const result=await finalizePresentation({
  workspaceDir:workspace,candidatePath:path.join(build,revision,'candidate.pptx'),
  finalPath:path.join(workspace,`第一版_20261031/Vela伴星_决赛答辩_第一版_${revision}_字体兼容修复.pptx`),
  pythonExecutable:'C:/Users/Lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
  integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
  layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','24384000,13716000','--validate-bullet-geometry','--validate-heading-fit',...tableSlides.flatMap(n=>['--require-native-table-slide',String(n)])],
  explicitTotalSlideCount:24,requiredNativeTableOwnerSlides:tableSlides,
  requiredNativeChartOwnerSlides:[13],requiredEmbeddedWorkbookChartOwnerSlides:[13],
  materializeLiteralChartWorkbooks:false,
  fontPolicy:{basis:'design',families:['Arial','Microsoft YaHei'],scriptFonts:{ea:'Microsoft YaHei',cs:'Arial',symbol:'Arial'}},
  verifyArtifactToolImport:true,receiptPath:path.join(build,revision,'validation.json'),
});
console.log(JSON.stringify(result,null,2));
