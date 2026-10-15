-- Collider: records collision events so the engine's contact plumbing can be
-- verified end to end. The counters are per-script-instance, so each entity
-- using this script keeps its own tally.
local M = {}

M.Hits = 0
M.Separations = 0
M.LastOtherName = ""
M.LastNormalY = 0.0

function M.OnCreate(entity)
    Log("Collider ready for " .. entity:GetName())
end

function M.OnCollision(entity, other, nx, ny, nz)
    M.Hits = M.Hits + 1
    M.LastNormalY = ny

    -- `other` is nil when the partner body has no entity.
    if other ~= nil then
        M.LastOtherName = other:GetName()
    else
        M.LastOtherName = "<none>"
    end
end

function M.OnCollisionEnd(entity, other, nx, ny, nz)
    M.Separations = M.Separations + 1
end

return M