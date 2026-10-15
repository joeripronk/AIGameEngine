
local M = {}

function M.OnCreate(entity)
    entity:SetName("RenamedByLua")
    assert(entity:GetName() == "RenamedByLua")

    entity:SetPosition(1, 2, 3)
    local px, py, pz = entity:GetPosition()
    assert(px == 1 and py == 2 and pz == 3)

    entity:SetScale(2, 2, 2)
    local sx, sy, sz = entity:GetScale()
    assert(sx == 2 and sy == 2 and sz == 2)

    entity:SetRotation(10, 20, 30)

    entity:AddComponent("Light")
    assert(entity:HasComponent("Light"))
    entity:SetFloat("Light", "Intensity", 3.5)
    entity:SetVector3("Light", "Color", 1, 0.5, 0.25)

    entity:AddComponent("Rigidbody")
    assert(entity:HasComponent("Rigidbody"))
    entity:SetBool("Rigidbody", "FixedRotation", true)

    entity:SetString("Script", "ClassName", "ApiExercise")

    local created = Scene.CreateEntity("ApiCreated")
    assert(created ~= nil)
    created:SetPosition(9, 9, 9)

    M.created = created
    M.entity = entity
end

function M.OnUpdate(entity, dt)
    M.updateCount = (M.updateCount or 0) + 1
    local x, y, z = entity:GetPosition()
    entity:SetPosition(x + dt, y, z)
end

return M
