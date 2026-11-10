#pragma once

namespace NextDayz
{
    // Tap toggles FPS ADS; hold keeps ordinary shoulder aim. The caller uses
    // a monotonic wall clock so slow motion and frame rate do not change taps.
    class AimInput
    {
    public:
        void Press(double now, bool allowAds)
        {
            if (buttonDown_) return;
            buttonDown_ = true;
            pressedAt_ = now;
            tapEligible_ = allowAds;
            if (!allowAds) ads_ = false;
        }

        void Update(double now, double tapSeconds)
        {
            if (buttonDown_ && now - pressedAt_ >= tapSeconds)
            {
                tapEligible_ = false;
                ads_ = false;
            }
        }

        void Release(double now, double tapSeconds)
        {
            if (!buttonDown_) return;
            Update(now, tapSeconds);
            ads_ = tapEligible_ ? !ads_ : false;
            buttonDown_ = false;
            tapEligible_ = false;
        }

        void Cancel()
        {
            buttonDown_ = false;
            tapEligible_ = false;
            ads_ = false;
        }

        bool IsAiming() const { return buttonDown_ || ads_; }
        bool IsAds() const { return ads_; }

    private:
        bool buttonDown_ = false;
        bool tapEligible_ = false;
        bool ads_ = false;
        double pressedAt_ = 0.0;
    };
}
