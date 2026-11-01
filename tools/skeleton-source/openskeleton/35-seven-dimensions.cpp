#include "client/body_proportion_curves.h"
#include "client/height_body_reference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc!=3 || (std::strcmp(argv[1],"male") && std::strcmp(argv[1],"female"))) return 2;
    char* end=nullptr;
    const double height=std::strtod(argv[2],&end);
    if (!end || *end) return 2;
    const auto sex=std::strcmp(argv[1],"male")==0 ? gtc::native::BodyReferenceSex::male :
                                                   gtc::native::BodyReferenceSex::female;
    const auto profile=gtc::native::estimateHeightBodyReference(sex,height);
    gtc::native::BodyProportionCurves curves; std::string error;
    const std::string path="tools/skeleton-source/anthropometry/";
    if (!curves.load(path+"BODY_PART_CURVE_KNOTS.tsv",error) ||
        !curves.loadSelectedModels(path+"BODY_PART_SELECTED_MODELS.tsv",error)) {
        std::fprintf(stderr,"%s\n",error.c_str()); return 1;
    }
    const auto hip=curves.blendedNhanesDimensionCm(argv[1],"pelvis_surface_breadth",height);
    const auto shoulder=curves.blendedNhanesDimensionCm(argv[1],"shoulder_breadth",height);
    if (!profile || !hip || !shoulder) return 1;
    std::printf("head\t%.10f\nneck\t%.10f\ntorso\t%.10f\narm\t%.10f\nleg\t%.10f\n"
                "hip_width\t%.10f\nshoulder_width\t%.10f\n",
                profile->headCm,profile->neckCm,profile->torsoCm,profile->armCm,
                profile->legCm,*hip,*shoulder);
}
