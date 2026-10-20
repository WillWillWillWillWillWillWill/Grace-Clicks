#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <deque>

using namespace geode::prelude;

struct Snap {
    int frame;
    CheckpointObject* cp;
};

class $modify(GraceBase, GJBaseGameLayer) {
    struct Fields {
        int frame = 0;
        int lastClickFrame = -1;
        bool simulating = false;
    };

    // NOTE: signature varies between GD versions, check your bindings
    void handleButton(bool down, int button, bool isPlayer1) {
        if (down && !m_fields->simulating) m_fields->lastClickFrame = m_fields->frame;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(GracePlayLayer, PlayLayer) {
    struct Fields {
        std::deque<Snap> snaps;
        bool diedInSim = false;
        bool retrying = false;
    };

    void update(float dt) {
        auto f = static_cast<GraceBase*>(static_cast<GJBaseGameLayer*>(this));
        int grace = Mod::get()->getSettingValue<int64_t>("grace-frames");

        if (Mod::get()->getSettingValue<bool>("enabled") && grace > 0
            && !m_fields->retrying && !m_player1->m_isDead) {
            auto cp = this->createCheckpoint();
            cp->retain();
            m_fields->snaps.push_back({ f->m_fields->frame, cp });
            while ((int)m_fields->snaps.size() > grace * 2 + 2) {
                m_fields->snaps.front().cp->release();
                m_fields->snaps.pop_front();
            }
        }
        f->m_fields->frame++;
        PlayLayer::update(dt);
    }

    void destroyPlayer(PlayerObject* p, GameObject* obj) {
        if (m_fields->retrying) {            // death during a dry run
            m_fields->diedInSim = true;
            return;
        }
        if (Mod::get()->getSettingValue<bool>("enabled") && tryCorrect()) return;
        PlayLayer::destroyPlayer(p, obj);
    }

    bool tryCorrect() {
        auto f = static_cast<GraceBase*>(static_cast<GJBaseGameLayer*>(this));
        int grace = Mod::get()->getSettingValue<int64_t>("grace-frames");
        int click = f->m_fields->lastClickFrame;
        int now = f->m_fields->frame;
        if (grace <= 0 || click < 0 || now - click > grace + 2) return false;

        m_fields->retrying = true;
        f->m_fields->simulating = true;

        // try clicking earlier/later by 1..grace frames
        for (int d = 1; d <= grace; ++d) {
            for (int sign : { -1, 1 }) {
                int target = click + sign * d;
                for (auto& s : m_fields->snaps) {
                    if (s.frame != target) continue;
                    this->loadFromCheckpoint(s.cp);
                    m_fields->diedInSim = false;
                    m_player1->pushButton(PlayerButton::Jump);
                    // simulate forward past the original death point
                    for (int i = 0; i < (now - target) + grace + 5; ++i) {
                        if (i == 2) m_player1->releaseButton(PlayerButton::Jump);
                        PlayLayer::update(1.f / 60.f);
                        if (m_fields->diedInSim) break;
                    }
                    if (!m_fields->diedInSim) {   // found a survivable timing
                        m_fields->retrying = false;
                        f->m_fields->simulating = false;
                        log::info("Saved you! ({}ﾉ´ヮ`)ﾉ*: ･ﾟ shift {}", "", sign * d);
                        return true;
                    }
                }
            }
        }
        m_fields->retrying = false;
        f->m_fields->simulating = false;
        return false;   // nothing worked, let the death happen (╥﹏╥)
    }
};
