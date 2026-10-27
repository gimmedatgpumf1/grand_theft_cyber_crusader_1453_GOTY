#include "native.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>

namespace gtc::native {
namespace {

const char* kCoordinate[] = {
    "headWidth", "headHeight", "headDepth", "faceShape", "faceFullness", "forehead",
    "cheekBones", "cheekFullness", "cheekHeight", "jawWidth", "jawDrop", "chinHeight",
    "chinProjection", "chinBones", "chinCleft", "noseWidth", "noseLength", "noseVertical",
    "noseProjection", "noseBridge", "noseTip", "noseFlare", "nostrils", "nosePoint",
    "eyeSize", "eyeOpening", "eyeHeight2", "eyeHeight3", "eyeSpacing", "eyeDepth",
    "eyeTilt", "browHeight", "browAngle", "mouthWidth", "mouthVertical", "mouthFullness",
    "smile", "lipHeight", "upperLip", "lowerLip", "cupidsBow"
};

struct Evaluation {
    double score = 1e100;
    double objective = 1e100;
    std::vector<double> residual;
};

std::vector<Vec2> normalize(const std::vector<Vec2>& points, int eyeA, int eyeB) {
    std::vector<Vec2> result = points;
    const Vec2 midpoint{(points[eyeA].x + points[eyeB].x) * 0.5,
                        (points[eyeA].y + points[eyeB].y) * 0.5};
    const double ex = points[eyeB].x - points[eyeA].x;
    const double ey = points[eyeB].y - points[eyeA].y;
    const double scale = std::max(1e-12, std::hypot(ex, ey));
    const double angle = -std::atan2(ey, ex);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    for (auto& point : result) {
        const double x = point.x - midpoint.x;
        const double y = point.y - midpoint.y;
        point = {(c * x - s * y) / scale, (s * x + c * y) / scale};
    }
    return result;
}

bool solve(std::vector<double> matrix, std::vector<double>& rhs, int n) {
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        for (int row = column + 1; row < n; ++row) {
            if (std::abs(matrix[row * n + column]) > std::abs(matrix[pivot * n + column])) pivot = row;
        }
        if (std::abs(matrix[pivot * n + column]) < 1e-14) return false;
        if (pivot != column) {
            for (int k = column; k < n; ++k) std::swap(matrix[column * n + k], matrix[pivot * n + k]);
            std::swap(rhs[column], rhs[pivot]);
        }
        const double diagonal = matrix[column * n + column];
        for (int row = column + 1; row < n; ++row) {
            const double factor = matrix[row * n + column] / diagonal;
            if (std::abs(factor) < 1e-20) continue;
            for (int k = column + 1; k < n; ++k) matrix[row * n + k] -= factor * matrix[column * n + k];
            rhs[row] -= factor * rhs[column];
        }
    }
    for (int row = n - 1; row >= 0; --row) {
        double value = rhs[row];
        for (int k = row + 1; k < n; ++k) value -= matrix[row * n + k] * rhs[k];
        rhs[row] = value / matrix[row * n + row];
    }
    return true;
}

class Problem {
public:
    Problem(const HumanModel& human, const Reference& ref, const Preset& preset)
        : model(human), reference(ref), initial(preset), values(preset.morphs) {
        for (const char* name : kCoordinate) {
            const int index = model.specIndex(name);
            if (index >= 0) variables.push_back(index);
        }
        const auto a = std::find(model.fitIndices.begin(), model.fitIndices.end(), 33);
        const auto b = std::find(model.fitIndices.begin(), model.fitIndices.end(), 263);
        eyeA = static_cast<int>(a - model.fitIndices.begin());
        eyeB = static_cast<int>(b - model.fitIndices.begin());
        parameters.resize(variables.size() + 2);
        lower.resize(parameters.size());
        upper.resize(parameters.size());
        prior.resize(parameters.size());
        for (std::size_t i = 0; i < variables.size(); ++i) {
            const auto& spec = model.specs[variables[i]];
            parameters[i] = values[variables[i]] / 100.0;
            lower[i] = spec.minimum / 100.0;
            upper[i] = spec.maximum / 100.0;
        }
        parameters[variables.size()] = preset.pose.yaw;
        parameters[variables.size() + 1] = preset.pose.pitch;
        lower[variables.size()] = -1.15; upper[variables.size()] = 1.15;
        lower[variables.size() + 1] = -0.70; upper[variables.size() + 1] = 0.70;
        prior = parameters;
        weightSum = std::accumulate(model.fitWeights.begin(), model.fitWeights.end(), 0.0);
    }

    double regularization(std::size_t index) const {
        return index < variables.size() ? 0.00035 : 0.00001;
    }

    Evaluation evaluate(const std::vector<double>& parameters, const std::vector<double>* robust = nullptr) {
        ++evaluations;
        std::vector<double> shapedValues = values;
        for (std::size_t i = 0; i < variables.size(); ++i) shapedValues[variables[i]] = parameters[i] * 100.0;
        const std::vector<Vec3> points = model.shape(model.fitVertices, shapedValues);
        std::vector<Vec2> projected;
        projected.reserve(points.size());
        const double yaw = parameters[variables.size()];
        const double pitch = parameters[variables.size() + 1];
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double focal = 1.0 / std::tan(initial.pose.fovDegrees * 3.141592653589793 / 360.0);
        for (Vec3 point : points) {
            point = point - model.headCenter;
            const double x = cy * point.x + sy * point.z;
            const double z = -sy * point.x + cy * point.z;
            const double y = cp * point.y - sp * z;
            const double depth = initial.pose.distance - (sp * point.y + cp * z);
            const double safeDepth = std::max(0.03, depth);
            projected.push_back({focal * x / safeDepth, -focal * y / safeDepth});
        }
        const std::vector<Vec2> normalizedPoints = normalize(projected, eyeA, eyeB);
        Evaluation result;
        result.residual.reserve(model.fitIndices.size() * 2);
        double scoreSum = 0;
        double weightSum = 0;
        for (std::size_t i = 0; i < model.fitIndices.size(); ++i) {
            const int index = model.fitIndices[i];
            const double dx = normalizedPoints[i].x - reference.normalized[index].x;
            const double dy = normalizedPoints[i].y - reference.normalized[index].y;
            const double baseWeight = model.fitWeights[i];
            const double robustWeight = robust ? (*robust)[i] : 1.0;
            const double weight = baseWeight * robustWeight;
            scoreSum += baseWeight * (dx * dx + dy * dy);
            weightSum += baseWeight;
            result.residual.push_back(dx * std::sqrt(weight));
            result.residual.push_back(dy * std::sqrt(weight));
        }
        result.score = std::sqrt(scoreSum / std::max(1e-12, weightSum));
        double priorCost = 0;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const double weight = regularization(i);
            const double difference = parameters[i] - prior[i];
            priorCost += weight * difference * difference;
        }
        result.objective = result.score * result.score + priorCost;
        return result;
    }

    std::vector<double> robustWeights(const Evaluation& evaluation) {
        std::vector<double> result(model.fitIndices.size(), 1.0);
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double dx = evaluation.residual[i * 2];
            const double dy = evaluation.residual[i * 2 + 1];
            const double error = std::hypot(dx, dy) / std::sqrt(std::max(0.05, model.fitWeights[i]));
            result[i] = std::min(1.0, 0.025 / std::max(0.025, error));
        }
        return result;
    }

    Preset preset(const std::vector<double>& parameters, bool roundMorphs) const {
        Preset result = initial;
        for (std::size_t i = 0; i < variables.size(); ++i) {
            double value = parameters[i] * 100.0;
            if (roundMorphs) value = std::round(value);
            result.morphs[variables[i]] = value;
        }
        result.pose.yaw = parameters[variables.size()];
        result.pose.pitch = parameters[variables.size() + 1];
        return result;
    }

    const HumanModel& model;
    const Reference& reference;
    Preset initial;
    std::vector<double> values;
    std::vector<int> variables;
    std::vector<double> parameters;
    std::vector<double> lower;
    std::vector<double> upper;
    std::vector<double> prior;
    int eyeA = 0;
    int eyeB = 1;
    double weightSum = 1;
    std::uint64_t evaluations = 0;
};

} // namespace

FitResult fitPortrait(const HumanModel& model, const Reference& reference, const Preset& initial,
                      int iterations, bool verbose) {
    const auto started = std::chrono::steady_clock::now();
    Problem problem(model, reference, initial);
    FitResult result;
    result.preset = initial;
    result.initialScore = problem.evaluate(problem.parameters).score;

    // A cheap global pose pass avoids spending coupled iterations in a bad basin.
    std::vector<double> best = problem.parameters;
    Evaluation bestEvaluation = problem.evaluate(best);
    const double yawStart = problem.parameters[problem.variables.size()];
    const double pitchStart = problem.parameters[problem.variables.size() + 1];
    for (double yawOffset : {-0.35, -0.175, 0.0, 0.175, 0.35}) {
        for (double pitchOffset : {-0.20, 0.0, 0.20}) {
            std::vector<double> trial = problem.parameters;
            trial[problem.variables.size()] = std::clamp(yawStart + yawOffset, -1.15, 1.15);
            trial[problem.variables.size() + 1] = std::clamp(pitchStart + pitchOffset, -0.70, 0.70);
            const Evaluation evaluation = problem.evaluate(trial);
            if (evaluation.score < bestEvaluation.score) {
                best = trial;
                bestEvaluation = evaluation;
            }
        }
    }
    problem.parameters = best;
    double damping = 0.01;
    int completed = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const Evaluation base = problem.evaluate(problem.parameters);
        const std::vector<double> robust = problem.robustWeights(base);
        const Evaluation weightedBase = problem.evaluate(problem.parameters, &robust);
        const int n = static_cast<int>(problem.parameters.size());
        const int m = static_cast<int>(weightedBase.residual.size());
        std::vector<double> jacobian(static_cast<std::size_t>(m) * n, 0.0);
        for (int column = 0; column < n; ++column) {
            const double step = column < static_cast<int>(problem.variables.size()) ? 0.003 : 0.002;
            std::vector<double> trial = problem.parameters;
            double actual = step;
            trial[column] = std::clamp(trial[column] + step, problem.lower[column], problem.upper[column]);
            if (std::abs(trial[column] - problem.parameters[column]) < 1e-10) {
                trial[column] = std::clamp(trial[column] - step, problem.lower[column], problem.upper[column]);
                actual = -step;
            }
            actual = trial[column] - problem.parameters[column];
            if (std::abs(actual) < 1e-12) continue;
            const Evaluation shifted = problem.evaluate(trial, &robust);
            for (int row = 0; row < m; ++row) {
                jacobian[static_cast<std::size_t>(row) * n + column] =
                    (shifted.residual[row] - weightedBase.residual[row]) / actual;
            }
        }
        bool accepted = false;
        for (int attempt = 0; attempt < 4 && !accepted; ++attempt) {
            std::vector<double> matrix(static_cast<std::size_t>(n) * n, 0.0);
            std::vector<double> rhs(n, 0.0);
            for (int row = 0; row < m; ++row) {
                for (int a = 0; a < n; ++a) {
                    const double ja = jacobian[static_cast<std::size_t>(row) * n + a];
                    rhs[a] -= ja * weightedBase.residual[row];
                    for (int b = 0; b <= a; ++b) {
                        matrix[a * n + b] += ja * jacobian[static_cast<std::size_t>(row) * n + b];
                    }
                }
            }
            for (double& value : matrix) value /= problem.weightSum;
            for (double& value : rhs) value /= problem.weightSum;
            for (int a = 0; a < n; ++a) {
                for (int b = 0; b < a; ++b) matrix[b * n + a] = matrix[a * n + b];
                const double regular = problem.regularization(a);
                matrix[a * n + a] += regular + damping * (matrix[a * n + a] + 1e-5);
                rhs[a] -= regular * (problem.parameters[a] - problem.prior[a]);
            }
            if (!solve(matrix, rhs, n)) {
                damping *= 5;
                continue;
            }
            double largest = 0;
            for (int a = 0; a < n; ++a) {
                const double limit = a < static_cast<int>(problem.variables.size()) ? 0.10 : 0.12;
                rhs[a] = std::clamp(rhs[a], -limit, limit);
                largest = std::max(largest, std::abs(rhs[a]));
            }
            for (double fraction : {1.0, 0.5, 0.25, 0.1, 0.05}) {
                std::vector<double> trial = problem.parameters;
                for (int a = 0; a < n; ++a) trial[a] = std::clamp(trial[a] + rhs[a] * fraction,
                                                                    problem.lower[a], problem.upper[a]);
                const Evaluation evaluation = problem.evaluate(trial);
                if (evaluation.objective < base.objective - 1e-10) {
                    problem.parameters = trial;
                    accepted = true;
                    ++completed;
                    damping = std::max(0.000001, damping * 0.65);
                    if (verbose) {
                        std::cout << "  lm " << iteration << " score " << std::fixed << std::setprecision(6)
                                  << evaluation.score << " step " << largest * fraction << '\n';
                    }
                    break;
                }
            }
            if (!accepted) damping *= 5;
        }
        if (!accepted) {
            if (verbose) std::cout << "  lm " << iteration << " converged/no acceptable step\n";
            break;
        }
    }

    // Integer slider refinement keeps the exported creator values reproducible.
    for (int step : {4, 2, 1}) {
        for (int pass = 0; pass < 1; ++pass) {
            for (std::size_t variable = 0; variable < problem.variables.size(); ++variable) {
                const Evaluation base = problem.evaluate(problem.parameters);
                bool changed = false;
                std::vector<double> bestTrial = problem.parameters;
                Evaluation bestValue = base;
                for (int direction : {-1, 1}) {
                    std::vector<double> trial = problem.parameters;
                    trial[variable] = std::clamp(trial[variable] + direction * step / 100.0,
                                                 problem.lower[variable], problem.upper[variable]);
                    const Evaluation evaluation = problem.evaluate(trial);
                    if (evaluation.objective < bestValue.objective - 1e-10) {
                        bestValue = evaluation;
                        bestTrial = trial;
                        changed = true;
                    }
                }
                if (changed) problem.parameters = bestTrial;
            }
        }
    }
    for (std::size_t i = 0; i < problem.variables.size(); ++i) {
        problem.parameters[i] = std::round(problem.parameters[i] * 100.0) / 100.0;
    }
    result.preset = problem.preset(problem.parameters, true);
    result.score = problem.evaluate(problem.parameters).score;
    result.evaluations = problem.evaluations;
    result.iterations = completed;
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

} // namespace gtc::native
