#include "regime/hidden_markov_model.h"
#include "utils/math_utils.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace ste {

HiddenMarkovModel::HiddenMarkovModel(int num_states) : num_states_(num_states) {
    state_probs_.fill(0.0);
    state_probs_[0] = 1.0;
}

void HiddenMarkovModel::initMarketRegimeModel(Timeframe tf) {
    // 5 states: BullQuiet, BullVolatile, BearQuiet, BearVolatile, Transition
    num_states_ = 5;
    num_features_ = 4;  // returns, vol, volume_change, spread

    // Initial state distribution (start in BullQuiet)
    pi_ = {0.4, 0.15, 0.15, 0.05, 0.25};

    // Daily transition matrix (base)
    std::vector<std::vector<double>> daily_A = {
        {0.970, 0.012, 0.005, 0.003, 0.010},  // BullQuiet
        {0.015, 0.950, 0.005, 0.015, 0.015},  // BullVolatile
        {0.008, 0.005, 0.960, 0.012, 0.015},  // BearQuiet
        {0.005, 0.010, 0.008, 0.960, 0.017},  // BearVolatile
        {0.020, 0.020, 0.020, 0.020, 0.920},  // Transition
    };

    // Scale transition matrix for sub-daily timeframes
    // For sub-daily bars, regimes persist longer per-bar
    A_ = daily_A;
    if (tf != Timeframe::Daily) {
        double bars_per_day = periodsPerYear(tf) / 252.0;
        for (int i = 0; i < 5; ++i) {
            double daily_stay = daily_A[i][i];
            double bar_stay = std::pow(daily_stay, 1.0 / bars_per_day);
            double daily_leave = 1.0 - daily_stay;
            double bar_leave = 1.0 - bar_stay;
            for (int j = 0; j < 5; ++j) {
                if (i == j) {
                    A_[i][j] = bar_stay;
                } else {
                    A_[i][j] = (daily_leave > 1e-15) ?
                        daily_A[i][j] / daily_leave * bar_leave : bar_leave / 4.0;
                }
            }
        }
    }

    // Emission means [state][feature]: returns, vol, vol_of_vol, spread
    // Returns (feature 0) are per-bar, so scale by sqrt(dt/daily_dt)
    // Other features (vol level, vol-of-vol, spread) don't change with timeframe
    double return_scale = std::sqrt(timeframeDt(tf) / timeframeDt(Timeframe::Daily));
    mu_ = {
        { 0.0005 * return_scale,  0.12,  0.0,   1.0},   // BullQuiet
        { 0.0008 * return_scale,  0.25,  0.5,   1.5},   // BullVolatile
        {-0.0003 * return_scale,  0.15, -0.1,   1.8},   // BearQuiet
        {-0.0015 * return_scale,  0.40,  1.0,   3.5},   // BearVolatile
        { 0.0000,                 0.20,  0.3,   2.0},   // Transition
    };

    // Emission standard deviations — returns scale with sqrt(dt)
    sigma_ = {
        {0.008 * return_scale, 0.03, 0.5, 0.3},   // BullQuiet: tight
        {0.015 * return_scale, 0.08, 0.8, 0.5},   // BullVolatile: wide
        {0.010 * return_scale, 0.04, 0.6, 0.5},   // BearQuiet
        {0.025 * return_scale, 0.12, 1.0, 1.0},   // BearVolatile: very wide
        {0.012 * return_scale, 0.06, 0.7, 0.6},   // Transition
    };

    state_probs_ = {0.4, 0.15, 0.15, 0.05, 0.25};
}

double HiddenMarkovModel::gaussianPdf(double x, double mean, double std_dev) const {
    if (std_dev < 1e-15) return (std::abs(x - mean) < 1e-15) ? 1e10 : 0.0;
    double z = (x - mean) / std_dev;
    return math::norm_pdf(z) / std_dev;
}

double HiddenMarkovModel::emissionProb(int state, const std::vector<double>& obs) const {
    double prob = 1.0;
    int n = std::min(static_cast<int>(obs.size()), num_features_);
    for (int f = 0; f < n; ++f) {
        prob *= gaussianPdf(obs[f], mu_[state][f], sigma_[state][f]);
    }
    return std::max(prob, 1e-300);
}

std::array<double, 5> HiddenMarkovModel::update(const std::vector<double>& observation) {
    // Forward step: update state probabilities given new observation
    std::array<double, 5> new_probs;
    new_probs.fill(0.0);

    for (int j = 0; j < num_states_; ++j) {
        double sum = 0.0;
        for (int i = 0; i < num_states_; ++i) {
            sum += state_probs_[i] * A_[i][j];
        }
        new_probs[j] = sum * emissionProb(j, observation);
    }

    // Normalize
    double total = 0.0;
    for (int j = 0; j < num_states_; ++j) total += new_probs[j];
    if (total > 0) {
        for (int j = 0; j < num_states_; ++j) new_probs[j] /= total;
    } else {
        // Fallback to uniform
        for (int j = 0; j < num_states_; ++j) new_probs[j] = 1.0 / num_states_;
    }

    state_probs_ = new_probs;
    return state_probs_;
}

int HiddenMarkovModel::mostLikelyState() const {
    return static_cast<int>(std::max_element(state_probs_.begin(),
                                              state_probs_.end()) - state_probs_.begin());
}

double HiddenMarkovModel::transitionProbability() const {
    int current = mostLikelyState();
    return 1.0 - A_[current][current];
}

std::vector<int> HiddenMarkovModel::viterbi(const std::vector<std::vector<double>>& observations) const {
    int T = static_cast<int>(observations.size());
    if (T == 0) return {};

    // Viterbi in log space
    std::vector<std::vector<double>> V(T, std::vector<double>(num_states_));
    std::vector<std::vector<int>> ptr(T, std::vector<int>(num_states_, 0));

    // Init
    for (int s = 0; s < num_states_; ++s) {
        V[0][s] = std::log(pi_[s] + 1e-300) + std::log(emissionProb(s, observations[0]));
    }

    // Recurse
    for (int t = 1; t < T; ++t) {
        for (int j = 0; j < num_states_; ++j) {
            double best = -1e300;
            int best_s = 0;
            for (int i = 0; i < num_states_; ++i) {
                double val = V[t-1][i] + std::log(A_[i][j] + 1e-300);
                if (val > best) { best = val; best_s = i; }
            }
            V[t][j] = best + std::log(emissionProb(j, observations[t]));
            ptr[t][j] = best_s;
        }
    }

    // Backtrack
    std::vector<int> path(T);
    path[T-1] = static_cast<int>(std::max_element(V[T-1].begin(), V[T-1].end()) - V[T-1].begin());
    for (int t = T - 2; t >= 0; --t) {
        path[t] = ptr[t+1][path[t+1]];
    }
    return path;
}

double HiddenMarkovModel::forward(const std::vector<std::vector<double>>& observations) const {
    int T = static_cast<int>(observations.size());
    if (T == 0) return 0.0;

    std::vector<double> alpha(num_states_);
    for (int s = 0; s < num_states_; ++s) {
        alpha[s] = pi_[s] * emissionProb(s, observations[0]);
    }

    for (int t = 1; t < T; ++t) {
        std::vector<double> new_alpha(num_states_, 0.0);
        for (int j = 0; j < num_states_; ++j) {
            for (int i = 0; i < num_states_; ++i) {
                new_alpha[j] += alpha[i] * A_[i][j];
            }
            new_alpha[j] *= emissionProb(j, observations[t]);
        }
        // Scale to prevent underflow
        double scale = *std::max_element(new_alpha.begin(), new_alpha.end());
        if (scale > 0) {
            for (double& a : new_alpha) a /= scale;
        }
        alpha = new_alpha;
    }

    return std::accumulate(alpha.begin(), alpha.end(), 0.0);
}

void HiddenMarkovModel::baumWelch(const std::vector<std::vector<double>>& observations,
                                    int max_iterations, double tolerance) {
    int T = static_cast<int>(observations.size());
    if (T < 2) return;

    for (int iter = 0; iter < max_iterations; ++iter) {
        // Forward pass
        std::vector<std::vector<double>> alpha(T, std::vector<double>(num_states_));
        std::vector<double> scales(T, 0.0);

        for (int s = 0; s < num_states_; ++s) {
            alpha[0][s] = pi_[s] * emissionProb(s, observations[0]);
        }
        scales[0] = std::accumulate(alpha[0].begin(), alpha[0].end(), 0.0);
        if (scales[0] > 0) for (double& a : alpha[0]) a /= scales[0];

        for (int t = 1; t < T; ++t) {
            for (int j = 0; j < num_states_; ++j) {
                alpha[t][j] = 0;
                for (int i = 0; i < num_states_; ++i)
                    alpha[t][j] += alpha[t-1][i] * A_[i][j];
                alpha[t][j] *= emissionProb(j, observations[t]);
            }
            scales[t] = std::accumulate(alpha[t].begin(), alpha[t].end(), 0.0);
            if (scales[t] > 0) for (double& a : alpha[t]) a /= scales[t];
        }

        // Backward pass
        std::vector<std::vector<double>> beta(T, std::vector<double>(num_states_, 1.0));
        for (int t = T - 2; t >= 0; --t) {
            for (int i = 0; i < num_states_; ++i) {
                beta[t][i] = 0;
                for (int j = 0; j < num_states_; ++j)
                    beta[t][i] += A_[i][j] * emissionProb(j, observations[t+1]) * beta[t+1][j];
            }
            if (scales[t+1] > 0) for (double& b : beta[t]) b /= scales[t+1];
        }

        // Compute gamma and xi, update parameters
        std::vector<std::vector<double>> gamma(T, std::vector<double>(num_states_));
        for (int t = 0; t < T; ++t) {
            double sum = 0;
            for (int s = 0; s < num_states_; ++s) {
                gamma[t][s] = alpha[t][s] * beta[t][s];
                sum += gamma[t][s];
            }
            if (sum > 0) for (double& g : gamma[t]) g /= sum;
        }

        // Update pi
        for (int s = 0; s < num_states_; ++s) pi_[s] = gamma[0][s];

        // Update A
        for (int i = 0; i < num_states_; ++i) {
            double denom = 0;
            for (int t = 0; t < T - 1; ++t) denom += gamma[t][i];
            if (denom < 1e-300) continue;
            for (int j = 0; j < num_states_; ++j) {
                double numer = 0;
                for (int t = 0; t < T - 1; ++t) {
                    double total = 0;
                    for (int k = 0; k < num_states_; ++k)
                        total += alpha[t][k] * beta[t][k];
                    if (total < 1e-300) continue;
                    numer += alpha[t][i] * A_[i][j] * emissionProb(j, observations[t+1]) *
                             beta[t+1][j] / total;
                }
                A_[i][j] = std::max(1e-6, numer / denom);
            }
            // Normalize row
            double row_sum = std::accumulate(A_[i].begin(), A_[i].end(), 0.0);
            if (row_sum > 0) for (double& a : A_[i]) a /= row_sum;
        }

        // Update emission params (mu, sigma)
        for (int s = 0; s < num_states_; ++s) {
            double gamma_sum = 0;
            for (int t = 0; t < T; ++t) gamma_sum += gamma[t][s];
            if (gamma_sum < 1e-300) continue;

            for (int f = 0; f < num_features_; ++f) {
                double mu_num = 0, sig_num = 0;
                for (int t = 0; t < T; ++t) {
                    mu_num += gamma[t][s] * observations[t][f];
                }
                mu_[s][f] = mu_num / gamma_sum;

                for (int t = 0; t < T; ++t) {
                    double diff = observations[t][f] - mu_[s][f];
                    sig_num += gamma[t][s] * diff * diff;
                }
                sigma_[s][f] = std::max(1e-4, std::sqrt(sig_num / gamma_sum));
            }
        }
    }
}

} // namespace ste
