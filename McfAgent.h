#ifndef MCF_AGENT_H
#define MCF_AGENT_H

#include <string>
#include <functional>
#include <memory>
#include <future>
#include <cstdint>

namespace rayconnect {

class McfAgent {
public:
    using DataType = std::string;
    using SubscriptionHandle = uint64_t;

    class ITopicListener;

    explicit McfAgent();

    ~McfAgent();

    McfAgent(const McfAgent&) = delete;
    McfAgent& operator=(const McfAgent&) = delete;

    McfAgent(McfAgent&&) noexcept;
    McfAgent& operator=(McfAgent&&) noexcept;

    std::future<DataType> submit_data(DataType data);

    SubscriptionHandle subscribe(const std::string& topic_name, std::weak_ptr<ITopicListener> listener);
    void unsubscribe(SubscriptionHandle handle);

    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> mPimpl;
};

class McfAgent::ITopicListener {
public:
    virtual ~ITopicListener() = default;
    virtual void on_topic_data(const std::string& topic_name, const DataType& data) = 0;
};

} // namespace rayconnect

#endif // MCF_AGENT_H