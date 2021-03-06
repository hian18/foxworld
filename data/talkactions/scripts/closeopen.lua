function onSay(cid, words, param)
	local state = GAMESTATE_CLOSED
	if(words:sub(2, 2) == "o") then
		state = GAMESTATE_OPEN
	end

	local str = "Failed to " .. (state == GAMESTATE_CLOSED and "close" or "open") .. " server."
	if(doChangeGameState(state) ~= LUA_ERROR) then
		str = "Server has been " .. (state == GAMESTATE_CLOSED and "closed" or "opened") .. " successfully."
	end

	doPlayerSendTextMessage(cid, MESSAGE_STATUS_CONSOLE_BLUE, str)
	return TRUE
end
