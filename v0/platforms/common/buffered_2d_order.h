#pragma once

#include <cstdint>
#include <vector>

/** Records interleaved 2D flush order (sprite vs font groups) for Metal. */
enum class Tori2DFlushKind : uint8_t
{
    Sprite = 0,
    Font = 1,
};

struct Tori2DFlushOp
{
    Tori2DFlushKind kind;
    uint32_t group_index;
};

class Buffered2DOrder
{
public:
    void
    begin_pass()
    {
        ops_.clear();
    }

    void
    push_sprite(uint32_t group_index)
    {
        ops_.push_back(Tori2DFlushOp{ Tori2DFlushKind::Sprite, group_index });
    }

    void
    push_font(uint32_t group_index)
    {
        ops_.push_back(Tori2DFlushOp{ Tori2DFlushKind::Font, group_index });
    }

    const std::vector<Tori2DFlushOp>&
    ops() const
    {
        return ops_;
    }

    bool
    empty() const
    {
        return ops_.empty();
    }

private:
    std::vector<Tori2DFlushOp> ops_;
};
