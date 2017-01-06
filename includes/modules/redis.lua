require("redis.core")

local clients = setmetatable({}, {__mode = "v"})
local clientsnum = 0
local redisCreateClient = redis.CreateClient

function redis.CreateClient()
	local client, err = redisCreateClient()
	if not client then
		error(err)
	end

	table.insert(clients, client)
	clientsnum = clientsnum + 1
	return client
end

function redis.GetClientsTable()
	for i = 1, clientsnum do
		if clients[i] == nil then
			table.remove(clients, i)
			i = i - 1
			clientsnum = clientsnum - 1
		end
	end

	return clients
end

local meta = FindMetaTable("redis_client")

function meta:State(callback)
	return self:Send({"CLUSTER", "INFO"}, callback)
end

function meta:Save(callback)
	return self:Send("BGSAVE", callback)
end

function meta:LastSave(callback)
	return self:Send("LASTSAVE", callback)
end

function meta:Expire(key, secs, callback)
	return self:Send({"EXPIRE", key, secs}, callback)
end

function meta:Set(key, value, callback)
	return self:Send({"SET", key, value}, callback)
end

function meta:SetEx(key, secs, value, callback)
	return self:Send({"SETEX", key, secs, value}, callback)
end

function meta:Get(key, callback)
	return self:Send({"GET", key}, callback)
end

function meta:Exists(key, callback)
	local cmd = {"EXISTS"}
	if type(key) == "table" then
		for i = 1, #key do
			cmd[i + 1] = key[i]
		end
	else
		table.insert(cmd, key)
	end

	return self:Send(cmd, callback)
end

function meta:Delete(key, callback)
	local cmd = {"DEL"}
	if type(key) == "table" then
		for i = 1, #key do
			cmd[i + 1] = key[i]
		end
	else
		table.insert(cmd, key)
	end

	return self:Send(cmd, callback)
end

function meta:GetConfig(param, callback)
	return self:Send({"CONFIG", "GET", param}, callback)
end

function meta:SetConfig(param, value, callback)
	return self:Send({"CONFIG", "SET", param, value}, callback)
end
