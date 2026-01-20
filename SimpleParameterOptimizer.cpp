#include "SimpleParameterOptimizer.h"

#include <queue>
#include <utility>

#include "BattleEmulator.h"

#if defined(OPTIMIZE_MODE)

class ThreadPool {
public:
    explicit ThreadPool(size_t threads)
        : stop(false)
    {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                        });
                        if (stop && tasks.empty())
                            return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    auto enqueue(F&& f) -> std::future<typename std::invoke_result<F>::type> {
        using return_type = typename std::invoke_result<F>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(f)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};



SimpleParameterOptimizer::SimpleParameterOptimizer(
    Evaluator evaluator,
    uint64_t seed,
    int lambda,
    int mu,
    int threads
) : evaluator_(std::move(evaluator)),
    rng_(seed),
    lambda_(lambda),
    mu_(mu),
    threads_(threads),
    mean_(ids, 0.0),
    sigma_(ids, 1.0)
{
}

double SimpleParameterOptimizer::sampleNormal(double mean, double sigma) {
    std::normal_distribution<double> dist(mean, sigma);
    return dist(rng_);
}

SimpleParameterOptimizer::Result SimpleParameterOptimizer::run(int generations) {
    Result best;
    best.score = -1e100;

    for (int gen = 0; gen < generations; ++gen) {
        struct Candidate {
            std::vector<double> genes;
            double score{};
        };

        std::vector<Candidate> population(lambda_);
        std::vector<std::future<double>> futures;

        std::vector<Candidate> list{};

        for (int i = 0; i < lambda_; ++i) {
            population[i].genes.resize(ids);
            for (int d = 0; d < ids; ++d) {
                population[i].genes[d] = sampleNormal(mean_[d], sigma_[d]);
            }

            list.push_back(population[i]);

            futures.push_back(std::async(
                std::launch::async,
                [this, &population, i]() {
                    return evaluator_(population[i].genes, rng_());
                }
            ));
        }

        for (int i = 0; i < lambda_; ++i) {
            population[i].score = futures[i].get();
        }

        std::sort(population.begin(), population.end(),
            [](const Candidate& a, const Candidate& b) {
                return a.score > b.score;
            });

        if (population[0].score > best.score) {
            best.score = population[0].score;
            best.genes = population[0].genes;
            best.index = gen;

            std::cout << "Generation " << gen << ": " << best.score << std::endl;
        }

        // update mean
        for (int d = 0; d < ids; ++d) {
            double m = 0.0;
            for (int i = 0; i < mu_; ++i) {
                m += population[i].genes[d];
            }
            m /= mu_;
            mean_[d] = m;
        }

        // update sigma (simple)
        for (int d = 0; d < ids; ++d) {
            double v = 0.0;
            for (int i = 0; i < mu_; ++i) {
                double diff = population[i].genes[d] - mean_[d];
                v += diff * diff;
            }
            v /= mu_;
            sigma_[d] = std::max(1e-6, std::sqrt(v));
        }
    }

    return best;
}

void SimpleParameterOptimizer::printGenome(const std::vector<double>& genes) {
    constexpr int MAX_ID = SimpleParameterOptimizerNode::lastid;
    std::vector<double> tmp(MAX_ID + 1, 0.0);

    for (size_t i = 0; i < genes.size(); ++i) {
        int id = SimpleParameterOptimizerNode::TUNE_IDS[i];
        tmp[id] = genes[i];
    }

    std::cout << "constexpr std::array<double, " << (MAX_ID + 1) << "> GENOME = {\n";

    for (int id = 0; id <= MAX_ID; ++id) {
        const auto id1 = tmp[id];
        std::cout << id1;
        if (id != MAX_ID) std::cout << ",";
        if (id1 != 0.0) {
            std::cout << "\n    ";
        }
    }
    std::cout << "};\n";
}

#endif