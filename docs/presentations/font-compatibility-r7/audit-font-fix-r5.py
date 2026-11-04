"""Verify font edits and declared fit corrections preserve presentation content."""
from pathlib import Path
import hashlib,json,os,re,sys,zipfile,xml.etree.ElementTree as ET

base=Path(os.environ['A733_DEFENSE_BUILD_DIR']) if 'A733_DEFENSE_BUILD_DIR' in os.environ else Path(__file__).resolve().parent
source=base.parent/'第一版_20261031/Vela伴星_决赛答辩_第一版_r4.pptx'
target=Path(sys.argv[1]) if len(sys.argv)>1 else base/'r5/candidate.pptx'
revision=sys.argv[2] if len(sys.argv)>2 else 'r5'
out=base/revision/('candidate-font-audit.json' if target.name=='candidate.pptx' else 'final-font-audit.json')
NS='http://schemas.openxmlformats.org/drawingml/2006/main'
PN='http://schemas.openxmlformats.org/presentationml/2006/main'
font_tags={f'{{{NS}}}{t}' for t in ('latin','ea','cs','sym','font','buFont')}

def canonical_without_font_edits(data,normalize_architecture=False):
    root=ET.fromstring(data)
    if normalize_architecture:
        frames=0
        for shape in root.iter(f'{{{PN}}}sp'):
            identity=shape.find(f'{{{PN}}}nvSpPr/{{{PN}}}cNvPr')
            if identity is not None and identity.get('id') in {'13','17','21','25'}:
                ext=shape.find(f'{{{PN}}}spPr/{{{NS}}}xfrm/{{{NS}}}ext')
                assert ext.get('cy')=='2286000' and ext.get('cx')=='4826000'
                ext.set('cy','1905000');frames+=1
        assert frames==4
    for parent in root.iter():
        parent.attrib.pop('embedTrueTypeFonts',None)
        parent.attrib.pop('saveSubsetFonts',None)
        for child in list(parent):
            if child.tag in font_tags or child.tag==f'{{{PN}}}embeddedFontLst':
                parent.remove(child)
            elif child.tag.endswith('}Relationship') and child.get('Type','').endswith('/font'):
                parent.remove(child)
            elif child.tag.endswith('}Override') and child.get('PartName','').startswith('/ppt/fonts/'):
                parent.remove(child)
    for para in root.iter(f'{{{NS}}}p'):
        for item in list(para):
            if item.tag==f'{{{NS}}}br':para.remove(item)
        previous=None
        for item in list(para):
            if item.tag!=f'{{{NS}}}r':previous=None;continue
            pr=item.find(f'{{{NS}}}rPr');text=item.find(f'{{{NS}}}t')
            same=previous is not None and ET.tostring(previous.find(f'{{{NS}}}rPr'))==ET.tostring(pr)
            if same:
                prev_text=previous.find(f'{{{NS}}}t');prev_text.text=(prev_text.text or '')+(text.text or '')
                para.remove(item)
            else:previous=item
    return ET.tostring(root,encoding='utf-8')

with zipfile.ZipFile(source) as old,zipfile.ZipFile(target) as new:
    old_names={n for n in old.namelist() if not n.endswith('/')}
    new_names={n for n in new.namelist() if not n.endswith('/')}
    removed=old_names-new_names
    assert removed=={'ppt/fonts/font.dat','ppt/fonts/font2.dat'},removed
    assert not(new_names-old_names),new_names-old_names
    binary_kept=[];xml_kept=[];changed=[];font_count=0;soft_breaks=[]
    for name in sorted(new_names):
        a,b=old.read(name),new.read(name)
        if name.endswith(('.xml','.rels')):
            delta=len(ET.fromstring(b).findall(f'.//{{{NS}}}br'))-len(ET.fromstring(a).findall(f'.//{{{NS}}}br'))
            expected=int(revision!='r5' and name in {'ppt/slides/slide6.xml','ppt/slides/slide16.xml'})
            assert delta==expected,('unexpected soft break',name,delta)
            if delta:soft_breaks.append(name)
            permitted=revision=='r7' and name=='ppt/slides/slide6.xml'
            assert canonical_without_font_edits(a)==canonical_without_font_edits(b,permitted),('unexpected content/layout change',name)
            xml_kept.append(name)
            if name.startswith('ppt/'):
                for e in ET.fromstring(b).iter():
                    if e.tag in font_tags:
                        family=e.get('typeface')
                        assert family in {'Arial','Microsoft YaHei'},(name,family)
                        if e.tag==f'{{{NS}}}ea':assert family=='Microsoft YaHei'
                        if e.tag==f'{{{NS}}}latin':assert family=='Arial' and e.get('charset')=='0'
                        font_count+=1
        else:
            assert a==b,('binary changed',name)
            binary_kept.append(name)
        if a!=b:changed.append(name)
    slides=[n for n in new_names if re.fullmatch(r'ppt/slides/slide\d+\.xml',n)]
    assert len(slides)==24
    tables=sum(len(ET.fromstring(new.read(n)).findall(f'.//{{{NS}}}tbl')) for n in slides)
    assert tables==4
    workbooks=[n for n in binary_kept if n.endswith('.xlsx')]
    assert len(workbooks)==1
    result=dict(result='PASS_FONT_REPAIR_CONTENT_NOTES_IMAGES_WORKBOOK_PRESERVED',
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        target_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
        slides=24,native_tables=tables,fonts_checked=font_count,approved_soft_breaks=soft_breaks,
        geometry_changes={'slide6_frame_height':{'shape_ids':['13','17','21','25'],'from_emu':1905000,'to_emu':2286000}} if revision=='r7' else {},
        preserved_workbooks=workbooks,preserved_binary_parts=len(binary_kept),
        preserved_non_font_xml=len(xml_kept),changed_font_parts=changed,
        removed_conflicting_font_parts=sorted(removed),native_PowerPoint_tested=False)
    out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))
