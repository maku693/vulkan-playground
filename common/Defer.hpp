#include <functional>

class Defer {
public:
    explicit Defer(const std::function<void()>& defered) noexcept
        : m_defered(defered)
    {
    }

    explicit Defer(std::function<void()>&& defered) noexcept
        : m_defered(defered)
    {
    }

    ~Defer() { m_defered(); }

private:
    std::function<void()> m_defered;
};
