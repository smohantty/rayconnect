#ifndef MCF_AGENT_H
#define MCF_AGENT_H

#include <string>
#include <functional>
#include <memory>

namespace rayconnect {

class McfAgent {
public:
    using DataType = std::string;
    using ProcessingFunction = std::function<void(const DataType&)>;

    explicit McfAgent(ProcessingFunction processor);

    ~McfAgent();

    McfAgent(const McfAgent&) = delete;
    McfAgent& operator=(const McfAgent&) = delete;

    McfAgent(McfAgent&&) noexcept;
    McfAgent& operator=(McfAgent&&) noexcept;

    void submit_data(DataType data);

    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> mPimpl;
};

} // namespace rayconnect

#endif // MCF_AGENT_H