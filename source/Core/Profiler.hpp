#pragma once

#include "Core/Types.hpp"

#include <array>
#include <chrono>
#include <cstddef>

namespace RL
{
    enum class ProfileScope : u8
    {
        Physics = 0,
        Animation,
        Particles,
        Audio,
        Render,
        Count
    };

    class FrameProfiler
    {
    public:
        static constexpr usize kHistorySize = 120;
        static constexpr usize kScopeCount = static_cast<usize>(ProfileScope::Count);

        static FrameProfiler& Get() noexcept
        {
            static FrameProfiler instance;
            return instance;
        }

        void RecordScope(ProfileScope scope, f32 ms) noexcept
        {
            const usize idx = static_cast<usize>(scope);
            if (idx >= kScopeCount) return;

            m_lastMs[idx] = ms;
            m_history[idx][m_heads[idx]] = ms;
            m_heads[idx] = (m_heads[idx] + 1) % kHistorySize;
        }

        [[nodiscard]] f32 GetLastMs(ProfileScope scope) const noexcept
        {
            const usize idx = static_cast<usize>(scope);
            if (idx >= kScopeCount) return 0.0f;
            return m_lastMs[idx];
        }

        [[nodiscard]] const std::array<f32, kHistorySize>& GetHistory(ProfileScope scope) const noexcept
        {
            const usize idx = static_cast<usize>(scope);
            if (idx >= kScopeCount) return m_history[0];
            return m_history[idx];
        }

        [[nodiscard]] usize GetHistoryHead(ProfileScope scope) const noexcept
        {
            const usize idx = static_cast<usize>(scope);
            if (idx >= kScopeCount) return 0;
            return m_heads[idx];
        }

    private:
        FrameProfiler() = default;

        std::array<f32, kScopeCount> m_lastMs{};
        std::array<std::array<f32, kHistorySize>, kScopeCount> m_history{};
        std::array<usize, kScopeCount> m_heads{};
    };

    class ScopedProfileTimer
    {
    public:
        explicit ScopedProfileTimer(ProfileScope scope) noexcept
            : m_scope(scope)
            , m_start(std::chrono::high_resolution_clock::now())
        {
        }

        ~ScopedProfileTimer() noexcept
        {
            const auto end = std::chrono::high_resolution_clock::now();
            const f32 ms = std::chrono::duration<f32, std::milli>(end - m_start).count();
            FrameProfiler::Get().RecordScope(m_scope, ms);
        }

        ScopedProfileTimer(const ScopedProfileTimer&) = delete;
        ScopedProfileTimer& operator=(const ScopedProfileTimer&) = delete;

    private:
        ProfileScope m_scope;
        std::chrono::high_resolution_clock::time_point m_start;
    };
}
