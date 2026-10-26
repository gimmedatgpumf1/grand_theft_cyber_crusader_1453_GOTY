import json, math
from pathlib import Path
import cv2
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation

REPO = Path(__file__).resolve().parents[1]
WORK = REPO / 'tools/human-source/portrait-fitting'
ROOT = REPO / 'tools/human-source'
SPECS = json.loads((REPO / 'tools/human-morphs.json').read_text())
REFS = json.loads(WORK / 'reference-landmarks.json'.read_text())
CURRENT = json.loads(WORK / 'current-presets.json'.read_text())
CANON = np.array([list(map(float, line.split()[1:4])) for line in open(str(WORK / 'canonical_face_model.obj')) if line.startswith('v ')])
KEY = {1,4,10,13,14,33,133,159,145,362,263,386,374,61,291,152,234,454,70,300,63,293,105,334,66,296,107,336,55,285,65,295,52,282,53,283,46,276,
       48,115,220,45,64,98,327,344,278,440,275,50,280,101,330,205,425,17,0,37,39,40,267,269,270,321,405,314,84,181,91,146}
FIT = sorted(set(range(0, 468, 3)) | KEY)
EXCLUDE = {'neckWidth','shoulders','chest','torsoWidth','stomach','muscles','armWidth','legWidth','african','asian'}
SELECT = [i for i,s in enumerate(SPECS) if s['key'] not in EXCLUDE]

def read_target(path):
    out = {}
    for line in open(path):
        p=line.split()
        if len(p)==4 and p[0].isdigit(): out[int(p[0])] = np.array(list(map(float,p[1:4])))
    return out

class Mesh:
    def __init__(self, sex):
        self.sex=sex
        self.vertices=[];self.uvs=[];self.faces=[];group=''
        for line in open(ROOT/'base.obj'):
            p=line.split()
            if not p:continue
            if p[0]=='v':self.vertices.append(np.array(list(map(float,p[1:4]))))
            elif p[0]=='vt':self.uvs.append(tuple(map(float,p[1:3])))
            elif p[0]=='g':group=p[1]
            elif p[0]=='f' and group=='body':self.faces.append([tuple(int(n)-1 for n in x.split('/')[:2]) for x in p[1:]])
        self.vertices=np.array(self.vertices)
        self.base_macro=read_target(ROOT/f'caucasian-{sex}-young.target')
        for i,d in self.base_macro.items():self.vertices[i]+=d
        self.skeleton=json.load(open(ROOT/'default.mhskel'))
        self.bones=self.skeleton['bones']
        body_ids={v for f in self.faces for v,u in f}
        self.scale=1.8/(max(self.vertices[list(body_ids),1])-min(self.vertices[list(body_ids),1]))
        self.ground=-min(self.vertices[list(body_ids),1])
        self.regions_head=self.joint(self.bones['neck01']['head'])[1]-.07
        self.head_ids=np.array(sorted(i for i in body_ids if self.vertices[i,1]>self.regions_head-.25))
        self.eye_l=self.joint(self.bones['eye.L']['head']);self.eye_r=self.joint(self.bones['eye.R']['head'])
        self.eye_source={side:self.skeleton['joints'][self.bones[f'eye.{side}']['head']] for side in ('L','R')}
        self.neck_source=self.skeleton['joints'][self.bones['neck01']['head']]
        source_center=(CANON[33]+CANON[263])/2
        target_center=(self.eye_l+self.eye_r)/2
        self.translation=target_center-self.scale*source_center
        aligned=CANON*self.scale+self.translation
        tree=cKDTree(self.vertices[self.head_ids])
        distance,nearest=tree.query(aligned)
        self.canonical_original=self.head_ids[nearest]
        self.map_distance=distance
        self.original_uv={}
        for face in self.faces:
            for original,uv in face:
                if original not in self.original_uv:self.original_uv[original]=self.uvs[uv]
        self.cache={}
        print(sex,'mapping distance median/max',np.median(distance),max(distance),'key max',max(distance[list(KEY)]))
    def joint(self,name):
        ids=self.skeleton['joints'][name]
        return self.vertices[ids].mean(axis=0)
    def raw_morph(self,paths):
        combined={}
        for relative in paths:
            if relative.startswith('@'):
                group=relative[1:];macro='caucasian' if group=='old' else group
                filename=f"{macro}-{self.sex}-"+('old.target' if group=='old' else 'young.target')
                path=ROOT/'targets'/'macrodetails'/filename
            else:path=ROOT/'targets'/relative
            key=str(path)
            if key not in self.cache:self.cache[key]=read_target(path)
            target=self.cache[key]
            if relative.startswith('@'):
                difference={i:target.get(i,np.zeros(3))-self.base_macro.get(i,np.zeros(3)) for i in set(target)|set(self.base_macro)}
                reference=np.array([difference.get(i,np.zeros(3)) for i in self.neck_source]).mean(axis=0)
                for i,d in difference.items():
                    if self.vertices[i,1]>=self.regions_head-.20:combined[i]=combined.get(i,np.zeros(3))+d-reference
            else:
                for i,d in target.items():combined[i]=combined.get(i,np.zeros(3))+d
        return combined

MESHES={sex:Mesh(sex) for sex in ('male','female')}
OUTPUT={}
FIXED={}
for key,ref in REFS.items():
    sex='male' if CURRENT[key]['male'] else 'female'
    mesh=MESHES[sex]
    fixed=mesh.vertices.copy()
    for index,spec in enumerate(SPECS):
        if spec['key'] not in EXCLUDE:continue
        weight=CURRENT[key]['morphs'].get(spec['key'],0)/100
        if not weight:continue
        raw=mesh.raw_morph(spec['positive' if weight>0 else 'negative'])
        for vertex,delta in raw.items():fixed[vertex]+=delta*weight
    FIXED[key]=fixed
    # Keep all reliable canonical points; the reference always contains the full 468-point face.
    ids=np.array([i for i in FIT if mesh.map_distance[i]<.38])
    original=mesh.canonical_original[ids]
    reference=np.array([ref['points'][i][:2] for i in ids])
    center=(mesh.eye_l+mesh.eye_r)/2
    base=fixed[original]-center
    deltas=[]
    for index in SELECT:
        spec=SPECS[index]
        directions=[]
        for direction in ('negative','positive'):
            raw=mesh.raw_morph(spec[direction]) if spec[direction] else {}
            directions.append(np.array([raw.get(i,np.zeros(3)) for i in original]))
        deltas.append(directions)
    initial=np.array([CURRENT[key]['morphs'].get(SPECS[i]['key'],0)/100 for i in SELECT])
    eye_pair=[list(ids).index(33),list(ids).index(263)]
    model_eye_distance=np.linalg.norm(base[eye_pair[0]]-base[eye_pair[1]])
    ref_eye_distance=np.linalg.norm(reference[eye_pair[0]]-reference[eye_pair[1]])
    scale0=ref_eye_distance/model_eye_distance
    eye_mid=(reference[eye_pair[0]]+reference[eye_pair[1]])/2
    model_eye_mid=(base[eye_pair[0]]+base[eye_pair[1]])/2
    tx0=eye_mid[0]-scale0*model_eye_mid[0]
    ty0=eye_mid[1]+scale0*model_eye_mid[1]
    x0=np.r_[0,0,0,math.log(scale0),tx0,ty0,initial]
    lower=np.r_[-1.2,-1.2,-.8,math.log(20),-ref['width'], -ref['height'], np.full(len(SELECT),-1.)]
    upper=np.r_[1.2,1.2,.8,math.log(2500),ref['width']*2,ref['height']*2,np.full(len(SELECT),1.)]
    for j,index in enumerate(SELECT):
        if SPECS[index]['min']==0:lower[6+j]=0
    weights=np.array([1.35 if i in KEY else .75 for i in ids])
    def projected(params, points=base, delta_values=deltas):
        rot=Rotation.from_euler('xyz',params[:3]).as_matrix()
        shaped=points.copy()
        for j in range(len(SELECT)):
            w=params[6+j]
            shaped+=delta_values[j][1 if w>=0 else 0]*w
        q=shaped@rot.T
        s=math.exp(params[3])
        return np.column_stack((params[4]+s*q[:,0],params[5]-s*q[:,1]))
    def residual(params):
        diff=(projected(params)-reference)*weights[:,None]
        prior=(params[6:]-initial)*.6
        regular=params[6:]*np.array([.45 if SPECS[index]['key']=='age' else 1.35 for index in SELECT])
        return np.r_[diff.ravel(),prior,regular]
    result=least_squares(residual,x0,bounds=(lower,upper),loss='soft_l1',f_scale=4,max_nfev=280,x_scale='jac')
    fit=projected(result.x)
    rmse=float(np.sqrt(np.mean(np.sum((fit-reference)**2,axis=1))))
    morphs={SPECS[index]['key']:int(round(result.x[6+j]*100)) for j,index in enumerate(SELECT) if abs(result.x[6+j])>.025}
    for fixed in ('african','asian'):
        if CURRENT[key]['morphs'].get(fixed):morphs[fixed]=CURRENT[key]['morphs'][fixed]
    OUTPUT[key]={'male':sex=='male','morphs':morphs,'rmse':rmse,'camera':result.x[:6].tolist()}
    image=cv2.imread(str(WORK / 'references' / ref['file']))
    scale=ref['width']/image.shape[1]
    if scale!=1:image=cv2.resize(image,None,fx=scale,fy=scale,interpolation=cv2.INTER_CUBIC)
    for p in reference:cv2.circle(image,tuple(np.round(p).astype(int)),2,(30,30,230),-1)
    for p in fit:cv2.circle(image,tuple(np.round(p).astype(int)),2,(60,240,70),-1)
    cv2.putText(image,f'{key} RMSE {rmse:.2f}px',(20,35),cv2.FONT_HERSHEY_SIMPLEX,.8,(255,255,255),2)
    cv2.imwrite(str(REPO/'build'/f'fit-{key}.png'),image)
    print(key,'rmse',round(rmse,3),'morphs',morphs)
WORK / 'fitted-presets.json'.write_text(json.dumps(OUTPUT,indent=2))

SIZE = 1024
for key,ref in REFS.items():
    sex='male' if CURRENT[key]['male'] else 'female'
    mesh=MESHES[sex]
    fitted=OUTPUT[key]
    shaped=FIXED[key].copy()
    for j,index in enumerate(SELECT):
        name=SPECS[index]['key']
        weight=fitted['morphs'].get(name,0)/100
        if not weight:continue
        raw=mesh.raw_morph(SPECS[index]['positive' if weight>0 else 'negative'])
        for vertex,delta in raw.items():shaped[vertex]+=delta*weight
    center=(mesh.eye_l+mesh.eye_r)/2
    rot=Rotation.from_euler('xyz',fitted['camera'][:3]).as_matrix()
    q=(shaped-center)@rot.T
    scale3=math.exp(fitted['camera'][3])
    projected=np.column_stack((fitted['camera'][4]+scale3*q[:,0],fitted['camera'][5]-scale3*q[:,1]))
    source_points=np.array([point[:2] for point in ref['points']],dtype=np.float32)
    image=cv2.imread(str(WORK / 'references' / ref['file']))
    image_scale=ref['width']/image.shape[1]
    if image_scale!=1:image=cv2.resize(image,None,fx=image_scale,fy=image_scale,interpolation=cv2.INTER_CUBIC)
    skin_samples=[]
    for index in (50,280,101,330):
        x,y=map(int,source_points[index])
        patch=image[max(0,y-4):y+5,max(0,x-4):x+5]
        if patch.size:skin_samples.append(np.median(patch.reshape(-1,3),axis=0))
    skin=np.median(np.array(skin_samples),axis=0).astype(np.float32)
    face_mask=np.zeros(image.shape[:2],dtype=np.uint8)
    hull=cv2.convexHull(np.round(source_points).astype(np.int32))
    cv2.fillConvexPoly(face_mask,hull,255,lineType=cv2.LINE_AA)
    face_alpha=cv2.GaussianBlur(face_mask.astype(np.float32)/255,(0,0),2)[:,:,None]
    image=np.clip(image.astype(np.float32)*face_alpha+skin[None,None,:]*(1-face_alpha),0,255).astype(np.uint8)
    accum=np.zeros((SIZE,SIZE,3),dtype=np.float32)
    weights=np.zeros((SIZE,SIZE),dtype=np.float32)
    used=0
    for face in mesh.faces:
        if min(shaped[v,1] for v,u in face)<mesh.regions_head-.22:continue
        for corner in range(1,len(face)-1):
            triangle=[face[0],face[corner],face[corner+1]]
            ids=[v for v,u in triangle]
            normal=np.cross(q[ids[1]]-q[ids[0]],q[ids[2]]-q[ids[0]])
            if normal[2]<=0:continue
            src=projected[ids].astype(np.float32)
            dst=np.array([[mesh.uvs[u][0]*SIZE,(1-mesh.uvs[u][1])*SIZE] for v,u in triangle],dtype=np.float32)
            if abs(cv2.contourArea(src))<.03 or abs(cv2.contourArea(dst))<.08:continue
            if max(cv2.arcLength(dst,True),cv2.arcLength(src,True))>800:continue
            x,y,w,h=cv2.boundingRect(dst)
            if w<1 or h<1 or w>400 or h>400:continue
            if x<0 or y<0 or x+w>SIZE or y+h>SIZE:continue
            shifted=dst-np.array([x,y],dtype=np.float32)
            matrix=cv2.getAffineTransform(src,shifted)
            warped=cv2.warpAffine(image,matrix,(w,h),flags=cv2.INTER_CUBIC,borderMode=cv2.BORDER_REFLECT101)
            mask=np.zeros((h,w),dtype=np.float32)
            cv2.fillConvexPoly(mask,np.round(shifted).astype(np.int32),1.0,lineType=cv2.LINE_AA)
            accum[y:y+h,x:x+w]+=warped.astype(np.float32)*mask[:,:,None]
            weights[y:y+h,x:x+w]+=mask
            used+=1
    covered=weights>0.001
    detail=np.zeros_like(accum)
    detail[covered]=accum[covered]/weights[covered,None]
    detail=skin+(detail-skin)*.90
    alpha=cv2.GaussianBlur(covered.astype(np.float32),(0,0),1.5)
    alpha=np.clip(alpha*1.4,0,1)[:,:,None]
    texture=np.clip(skin[None,None,:]*(1-alpha)+detail*alpha,0,255).astype(np.uint8)
    cv2.imwrite(str(REPO/'web/assets/human'/f'portrait-{key}.png'),texture)
    yy,xx=np.where(covered)
    x0=max(0,int(xx.min())-15);x1=min(SIZE,int(xx.max())+15)
    y0=max(0,int(yy.min())-15);y1=min(SIZE,int(yy.max())+15)
    crop=texture[y0:y1,x0:x1]
    factor=max(1,700//max(1,crop.shape[1]))
    if factor>1:crop=cv2.resize(crop,None,fx=factor,fy=factor,interpolation=cv2.INTER_NEAREST)
    cv2.imwrite(str(REPO/'build'/f'uv-{key}.png'),crop)
    print('direct baked',key,'triangles',used,'coverage',round(float(covered.mean()),3))
