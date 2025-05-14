#ifndef MCF_AGENT_H
#define MCF_AGENT_H

#include <string>
#include <functional>
#include <memory>
#include <future>

namespace rayconnect {

class McfAgent {
public:
    using DataType = std::string;
    using TopicCallback = std::function<void(const DataType& data)>;
    using SubscriptionHandle = uint64_t;

    explicit McfAgent();

    ~McfAgent();

    McfAgent(const McfAgent&) = delete;
    McfAgent& operator=(const McfAgent&) = delete;

    McfAgent(McfAgent&&) noexcept;
    McfAgent& operator=(McfAgent&&) noexcept;

    std::future<DataType> submit_data(DataType data);

    SubscriptionHandle subscribe(const std::string& topic_name, TopicCallback callback);
    void unsubscribe(SubscriptionHandle handle);

    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> mPimpl;
};

} // namespace rayconnect

#endif // MCF_AGENT_H