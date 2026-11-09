// All bodies use the same compound-cell contact routine. A cell is a box in
// body-local coordinates. The 15 separating axes cover its faces and edges.
bool cellContact(Body a,Body b,vec3 ca,vec3 ea,vec3 cb,vec3 eb,
                 out vec3 normal,out vec3 armA,out vec3 armB,out float depth) {
    mat3 A=mat3(rotate(a.rotation,vec3(1,0,0)),rotate(a.rotation,vec3(0,1,0)),rotate(a.rotation,vec3(0,0,1)));
    mat3 B=mat3(rotate(b.rotation,vec3(1,0,0)),rotate(b.rotation,vec3(0,1,0)),rotate(b.rotation,vec3(0,0,1)));
    vec3 worldA=a.positionMass.xyz+A*ca, worldB=b.positionMass.xyz+B*cb;
    vec3 delta=worldA-worldB;
    depth=1e30;
    for (int i=0;i<15;++i) {
        vec3 axis=i<3 ? A[i] : (i<6 ? B[i-3] : cross(A[(i-6)/3],B[(i-6)%3]));
        float len=length(axis);
        if (len<1e-5) continue;
        axis/=len;
        float d=dot(delta,axis);
        float overlap=dot(abs(transpose(A)*axis),ea)+dot(abs(transpose(B)*axis),eb)-abs(d);
        if (overlap<=0.0) return false;
        if (overlap<depth) { depth=overlap; normal=d<0.0 ? -axis : axis; }
    }
    // Use the centre of the overlap of the projected contact patch. This
    // avoids applying a face-on contact at an arbitrary far-away corner.
    vec3 basis=abs(normal.y)<0.8 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 t1=normalize(cross(normal,basis)),t2=cross(normal,t1);
    vec3 point=vec3(0);
    for (int k=0;k<2;++k) {
        vec3 t=k==0 ? t1 : t2;
        float aCenter=dot(worldA,t),bCenter=dot(worldB,t);
        float ar=dot(abs(transpose(A)*t),ea),br=dot(abs(transpose(B)*t),eb);
        float lo=max(aCenter-ar,bCenter-br),hi=min(aCenter+ar,bCenter+br);
        point+=t*((lo+hi)*0.5);
    }
    float an=dot(worldA,normal)-dot(abs(transpose(A)*normal),ea);
    float bn=dot(worldB,normal)+dot(abs(transpose(B)*normal),eb);
    point+=normal*((an+bn)*0.5);
    armA=point-a.positionMass.xyz; armB=point-b.positionMass.xyz;
    return true;
}
