-- Rotator: spins an entity around its Y axis and exercises the component API.
local M = {}
M.updateCount = 0

function M.OnCreate(entity)
    Log("Rotator created for " .. entity:GetName())
    entity:SetRotation(0, 0, 0)

    -- Exercise component accessors
    assert(entity:HasComponent("Transform"))
    entity:SetFloat("PBRMaterial", "Metallic", 0.5)
    entity:SetVector3("PBRMaterial", "Albedo", 1, 0, 0)
    entity:SetBool("Rigidbody", "FixedRotation", true)
end

function M.OnUpdate(entity, dt)
    M.updateCount = M.updateCount + 1
    local x, y, z = entity:GetRotation()
    entity:SetRotation(x, y + 90.0 * dt, z)
end

return M
