#pragma once
#include "core/types.h"

namespace ste {

class HiddenMarkovModel {
public:
    explicit HiddenMarkovModel(int num_states = 5);

    // Initialize with default market regime parameters
    // Timeframe controls transition probabilities and return emission scaling
    void initMarketRegimeModel(Timeframe tf = Timeframe::Daily);

    // Forward algorithm: compute observation probability
    double forward(const std::vector<std::vector<double>>& observations) const;

    // Viterbi: most likely state sequence
    std::vector<int> viterbi(const std::vector<std::vector<double>>& observations) const;

    // Baum-Welch: train model parameters
    void baumWelch(const std::vector<std::vector<double>>& observations,
                   int max_iterations = 100, double tolerance = 1e-6);

    // Online update: process single new observation
    std::array<double, 5> update(const std::vector<double>& observation);

    // Get current state probabilities
    std::array<double, 5> stateProbabilities() const { return state_probs_; }

    // Get transition matrix
    const std::vector<std::vector<double>>& transitionMatrix() const { return A_; }

    // Get most likely current state
    int mostLikelyState() const;

    // Probability of transitioning away from current state
    double transitionProbability() const;

    int numStates() const { return num_states_; }

private:
    int num_states_;
    std::vector<double> pi_;                    // initial state distribution
    std::vector<std::vector<double>> A_;        // transition matrix [from][to]
    std::vector<std::vector<double>> mu_;       // emission means [state][feature]
    std::vector<std::vector<double>> sigma_;    // emission std devs [state][feature]
    std::array<double, 5> state_probs_;         // current state probabilities
    int num_features_ = 4;                       // returns, vol, volume, spread

    double gaussianPdf(double x, double mean, double std_dev) const;
    double emissionProb(int state, const std::vector<double>& obs) const;
};

} // namespace ste
