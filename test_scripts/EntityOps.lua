
local M = {}

function M.OnCreate(entity)
    local e = Scene.CreateEntity("LuaCreated")
    e:SetPosition(1, 1, 1)
    M.created = e
end

function M.OnUpdate(entity, dt)
    if M.created then
        Scene.DestroyEntity(M.created)
        M.created = nil
    end
end

return M
