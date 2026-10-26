import json,re
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
work=repo/'tools/human-source/portrait-fitting'
path=repo/'web/js/appearance.js'
text=path.read_text()
fitted=json.loads((work/'fitted-presets.json').read_text())
current=json.loads((work/'current-presets.json').read_text())
specs=json.loads((repo/'tools/human-morphs.json').read_text())
exclude={'neckWidth','shoulders','chest','torsoWidth','stomach','muscles','armWidth','legWidth','african','asian'}
text=re.sub(r'    creator(Yaw|Pitch|Roll): [^\n]+\n','',text)
for key in fitted:
    merged={k:v for k,v in current[key]['morphs'].items() if k in exclude}
    for spec in specs:
        name=spec['key']
        if name in exclude:continue
        value=fitted[key]['morphs'].get(name,0)
        if value:merged[name]=value
    ordered=[k for k in current[key]['morphs'] if k in merged]+[k for k in merged if k not in current[key]['morphs']]
    body='{ '+', '.join(f'{k}: {merged[k]}' for k in ordered)+' }'
    pattern=rf'(  {key}: \{{.*?    morphs: face\()\{{.*?\}}(\) \}},)'
    text,count=re.subn(pattern,lambda match:match.group(1)+body+match.group(2),text,count=1,flags=re.S)
    if count!=1:raise RuntimeError((key,count))
    start=text.index(f'  {key}: {{')
    portrait=text.index('    portrait:',start)
    pose=f"    creatorYaw: {fitted[key]['camera'][1]:.8f},\n    creatorPitch: {fitted[key]['camera'][0]:.8f},\n    creatorRoll: {fitted[key]['camera'][2]:.8f},\n"
    text=text[:portrait]+pose+text[portrait:]
path.write_text(text)
