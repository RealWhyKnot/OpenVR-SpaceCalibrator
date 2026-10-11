#include "Hooking.h"

std::map<std::string, IHook*> IHook::hooks;

bool IHook::Exists(const std::string& name)
{
	return hooks.find(name) != hooks.end();
}

void IHook::Register(IHook* hook)
{
	hooks[hook->name] = hook;
}

void IHook::Unregister(IHook* hook)
{
	hooks.erase(hook->name);
}

bool IHook::DisableAll()
{
	for (auto& hook : hooks) {
		hook.second->QueueDisable();
	}
	if (MH_ApplyQueued() == MH_OK) {
		for (auto& hook : hooks) {
			hook.second->MarkUnhooked();
		}
		return true;
	}
	bool all = true;
	for (auto& hook : hooks) {
		all = hook.second->DisableNow() && all;
	}
	return all;
}

void IHook::DestroyAll()
{
	for (auto& hook : hooks) {
		hook.second->Destroy();
	}
	hooks.clear();
}
