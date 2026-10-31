#pragma once

// Surface-landmark planning reference for approximately term neonates.
// It does not scale a skeleton or supply skeletal joint-centre distances.
// See anthropometry/NEWBORN_SEGMENT_RESEARCH.md before using this data.
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace gtc::native {

class NewbornLimbProfile {
public:
    enum class Sex { male, female };
    struct Dimensions {
        double upperArmCm, forearmCm, handCm, thighCm, lowerLegCm, footCm;
        // Whole limb measurements use different endpoints; they are not sums
        // of the component measurements.
        double totalUpperLimbCm, totalLowerLimbCm;
    };

    // Sex is a property of the selected source figure, not an extra height
    // input. The 39-week subgroup is a term-neonate reference; the median
    // body lengths were 50 cm (male) and 49 cm (female).
    bool load(const std::string& path, Sex sex, std::string& error) {
        std::ifstream input(path); std::string line;
        if (!input || !std::getline(input,line)) { error="Cannot open newborn ratios: "+path; return false; }
        stripCR(line);
        if (line!="sex\tgestational_week\tdimension\tn\tmedian_percent\tq1_percent\tq3_percent") {
            error="Invalid newborn ratio header: "+path; return false;
        }
        const char* names[]={"upper_arm","forearm","hand","thigh","lower_leg","foot",
                             "total_upper_limb","total_lower_limb"};
        std::array<double,8> ratios{}; std::array<bool,8> present{};
        while (std::getline(input,line)) {
            stripCR(line); std::istringstream row(line); std::string label,dimension,extra;
            int week=0, n=0; double median=0,q1=0,q3=0;
            if (!(row>>label>>week>>dimension>>n>>median>>q1>>q3) || (row>>extra) ||
                !std::isfinite(median) || !std::isfinite(q1) || !std::isfinite(q3) ||
                n<=0 || q1<=0 || q1>median || median>q3 || q3>=100) {
                error="Invalid newborn ratio row"; return false;
            }
            const std::string expected=sex==Sex::male ? "male" : "female";
            if (label!=expected || week!=39) continue;
            bool found=false;
            for (std::size_t i=0;i<ratios.size();++i) if (dimension==names[i]) {
                if (present[i]) { error="Duplicate newborn ratio"; return false; }
                ratios[i]=median/100.0; present[i]=true; found=true; break;
            }
            if (!found) { error="Unknown newborn body dimension"; return false; }
        }
        if (input.bad()) { error="Cannot read newborn ratios: "+path; return false; }
        for (bool item:present) if (!item) { error="Incomplete newborn ratios"; return false; }
        ratios_=ratios; loaded_=true; error.clear(); return true;
    }

    // Recumbent crown-to-heel length; one-input approximation within a narrow
    // 48–52 cm term-neonate design window. Height alone cannot establish age,
    // gestation, or health, and median ratios do not predict every individual.
    std::optional<Dimensions> estimate(double lengthCm) const {
        if (!loaded_ || !std::isfinite(lengthCm) || lengthCm<48 || lengthCm>52) return std::nullopt;
        return Dimensions{lengthCm*ratios_[0],lengthCm*ratios_[1],lengthCm*ratios_[2],
                          lengthCm*ratios_[3],lengthCm*ratios_[4],lengthCm*ratios_[5],
                          lengthCm*ratios_[6],lengthCm*ratios_[7]};
    }
private:
    static void stripCR(std::string& text) { if (!text.empty() && text.back()=='\r') text.pop_back(); }
    std::array<double,8> ratios_{};
    bool loaded_=false;
};

} // namespace gtc::native
