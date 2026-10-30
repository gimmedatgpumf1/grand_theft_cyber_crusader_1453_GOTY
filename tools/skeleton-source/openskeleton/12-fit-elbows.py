"""Estimate fixed elbow flexion axes from each subject's articular surfaces.

Capitellar spheres and trochlear cross-section circles are geometric reference
landmarks. Small subject-specific clearance refinements were selected against
sampled source-triangle motions. These static fixed-axis estimates are NOT
measured individual kinematics or medical validation; notably, some source
bone intersections and high-flexion collisions remain. See README and the
optional motion audit. Requires NumPy and SciPy.
"""
from pathlib import Path
import json
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent

# dx/dy in millimetres relative to the trochlear circle, followed by two
# dimensionless offsets of the centre-to-centre axis in the perpendicular basis.
# They were selected against a sampled 0-to-source extension sweep, not from
# subject-specific motion scans; they do not guarantee collision-free movement.
REFINEMENT = {
    ('male', 'L'): (-.630, -2.250, .096, .082),
    ('male', 'R'): (2.202, -6.386, -.322, .347),
    ('female', 'L'): (5.911, -7.260, -.243, .393),
    ('female', 'R'): (.461, 1.273, .037, -.051),
}


def unit(v):
    n = np.linalg.norm(v)
    if not np.isfinite(n) or n < 1e-9: raise ValueError('degenerate direction')
    return v/n


def sphere(points):
    mean = points.mean(axis=0); p = points-mean
    solution, _, rank, _ = np.linalg.lstsq(np.c_[2*p, np.ones(len(p))], np.sum(p*p,axis=1), rcond=None)
    if rank != 4: raise ValueError('degenerate sphere samples')
    radius = np.sqrt(max(0,solution[3]+solution[:3]@solution[:3]))
    fit = least_squares(lambda x: np.linalg.norm(points-x[:3],axis=1)-x[3],
                        np.r_[mean+solution[:3],radius], loss='soft_l1', f_scale=.5, max_nfev=1000)
    if not fit.success or not 5 < fit.x[3] < 30: raise ValueError('invalid capitellar fit')
    return fit.x[:3], float(fit.x[3])


def circle(points, axis):
    first = unit(np.cross(axis,[1,0,0]) if abs(axis[0]) < .9 else np.cross(axis,[0,1,0]))
    second = np.cross(axis,first); mean = points.mean(axis=0); p=points-mean
    x, y = p@first, p@second
    solution, _, rank, _ = np.linalg.lstsq(np.c_[2*x,2*y,np.ones(len(x))], x*x+y*y, rcond=None)
    if rank != 3: raise ValueError('degenerate trochlear circle')
    radius = np.sqrt(max(0,solution[2]+solution[0]**2+solution[1]**2))
    fit = least_squares(lambda v: np.hypot(x-v[0],y-v[1])-v[2], np.r_[solution[:2],radius],
                        loss='soft_l1', f_scale=.5, max_nfev=500)
    if not fit.success or not 5 < fit.x[2] < 25: raise ValueError('invalid trochlear fit')
    return mean + fit.x[0]*first + fit.x[1]*second, float(fit.x[2])


def main():
    for sex, filename, subject in (('male','male-N197925.npz','N197925'),('female','skelly-out.npz','N186416')):
        shift=np.array(json.loads((BASE/f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
        shoulders=json.loads((BASE/f'skeleton-{sex}-shoulder-fit.json').read_text())['shoulders']
        doc={'subject':subject, 'clinical_validation':False,
             'method':'capitellar sphere and trochlear circle with geometric clearance refinement', 'elbows':{}}
        rows=['# Approximate elbow axes in registered metres; not a validated medical motion model.',
              '# side pivot_x pivot_y pivot_z axis_x axis_y axis_z source_flexion_degrees']
        with np.load(HERE/'data'/filename) as source:
            for side in ('L','R'):
                humerus=source['v_Humerus_'+side].astype(float)
                ulna=source['v_Ulna_'+side].astype(float)
                radius=source['v_Radius_'+side].astype(float)
                cap_points=humerus[cKDTree(radius).query(humerus)[0]<6]
                troch_points=humerus[cKDTree(ulna).query(humerus)[0]<5]
                if min(len(cap_points),len(troch_points))<50: raise ValueError('insufficient articular samples')
                cap, cap_radius=sphere(cap_points); troch, _=sphere(troch_points); direction=unit(cap-troch)
                for _ in range(5):
                    troch, troch_radius=circle(troch_points,direction)
                    direction=unit(cap-troch)
                first=unit(np.cross(direction,[1,0,0]) if abs(direction[0])<.9 else np.cross(direction,[0,1,0]))
                second=np.cross(direction,first)
                dx,dy,ax,ay=REFINEMENT[(sex,side)]
                pivot=troch+dx*first+dy*second
                axis=unit(direction+ax*first+ay*second)
                shoulder=np.array(shoulders[side]['source_center_mm']); up=unit(shoulder-pivot)
                down=unit(ulna.mean(axis=0)-pivot)
                p=unit(-up-axis*np.dot(-up,axis)); q=unit(down-axis*np.dot(down,axis))
                angle=float(np.arctan2(axis@np.cross(p,q),p@q))
                if angle < 0: axis=-axis; angle=-angle
                angle=float(np.degrees(angle))
                if not 0 < angle < 150: raise ValueError('invalid estimated source angle')
                game_pivot=np.array([pivot[0],pivot[2],-pivot[1]])/1000+shift
                game_axis=np.array([axis[0],axis[2],-axis[1]])
                row={'source_pivot_mm':pivot.tolist(), 'source_axis':axis.tolist(),
                     'center_m':game_pivot.tolist(), 'axis':game_axis.tolist(), 'source_angle_deg':angle,
                     'capitellum_center_mm':cap.tolist(), 'capitellum_radius_mm':cap_radius,
                     'trochlear_center_mm':troch.tolist(), 'trochlear_radius_mm':troch_radius,
                     'clearance_refinement':list(REFINEMENT[(sex,side)]),
                     'axis_to_capitellum_mm':float(np.linalg.norm(np.cross(cap-pivot,axis)))}
                doc['elbows'][side]=row
                rows.append(side+' '+' '.join(f'{x:.9f}' for x in [*game_pivot,*game_axis,angle]))
        (BASE/f'skeleton-{sex}.elbows').write_text('\n'.join(rows)+'\n')
        (BASE/f'skeleton-{sex}-elbow-fit.json').write_text(json.dumps(doc,indent=2)+'\n')
        print(sex, ', '.join(f'{s}: {doc["elbows"][s]["source_angle_deg"]:.1f}°' for s in ('L','R')))


if __name__=='__main__': main()
