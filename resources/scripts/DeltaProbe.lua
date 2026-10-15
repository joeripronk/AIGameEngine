-- Verifies GetDeltaTime, which AGENTS.md documented in the Lua API but which
-- had no binding at all - a script calling it got "attempt to call a nil value".
--
-- Both call forms are exercised: with the entity and with no argument.

local M = {}

-- What OnUpdate saw.
M.DeltaInUpdate = nil
M.DeltaFromSelf = nil

-- What a non-updating entity reports, and what the frame reports overall.
M.DeltaFromOther = nil
M.DeltaNoArg = nil

-- Every distinct delta seen, so a caller can tell the value is real.
M.Samples = {}

local entity_under_test

function M.OnCreate(entity)
    entity_under_test = entity

    -- Before any frame has been simulated, there is no delta to report.
    M.DeltaBeforeUpdate = GetDeltaTime()
end

function M.OnUpdate(entity, dt)
    -- The explicit form, from inside the entity being updated.
    M.DeltaInUpdate = GetDeltaTime()

    -- The dt argument the engine passed, for comparison.
    M.DeltaArgument = dt

    -- The same entity named explicitly.
    M.DeltaFromSelf = GetDeltaTime(entity)

    -- With no entity in hand: the current frame's.
    M.DeltaNoArg = GetDeltaTime()

    table.insert(M.Samples, dt)
end

function M.ProbeFromOther(other)
    -- An entity that is not being updated must not report a stale value as if
    -- it were current.
    M.DeltaFromOther = GetDeltaTime(other)
end

return M