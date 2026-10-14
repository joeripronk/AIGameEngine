-- Mover: moves an entity forward along +X over time and logs its name.
local M = {}

function M.OnCreate(entity)
    Log("Mover created for " .. entity:GetName())
    entity:SetPosition(0, 0, 0)
end

function M.OnUpdate(entity, dt)
    local x, y, z = entity:GetPosition()
    entity:SetPosition(x + dt, y, z)
end

function M.OnDestroy(entity)
    Log("Mover destroyed for " .. entity:GetName())
end

return M
