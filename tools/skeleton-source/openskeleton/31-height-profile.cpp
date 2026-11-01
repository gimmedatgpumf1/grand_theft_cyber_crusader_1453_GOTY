#include "client/height_body_reference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc,char** argv) {
    if (argc!=3 || (std::strcmp(argv[1],"male") && std::strcmp(argv[1],"female"))) {
        std::fprintf(stderr,"Usage: height-profile male|female HEIGHT_CM\n"); return 2;
    }
    char* end=nullptr;
    const double h=std::strtod(argv[2],&end);
    if (!end || *end!='\0') return 2;
    const auto sex=std::strcmp(argv[1],"male")==0 ? gtc::native::BodyReferenceSex::male :
                                                    gtc::native::BodyReferenceSex::female;
    const auto p=gtc::native::estimateHeightBodyReference(sex,h);
    if (!p) { std::fprintf(stderr,"Unable to estimate profile for this height\n"); return 1; }
    std::printf("head\t%.10f\nneck\t%.10f\ntorso\t%.10f\narm\t%.10f\nleg\t%.10f\n",
                p->headCm,p->neckCm,p->torsoCm,p->armCm,p->legCm);
}
