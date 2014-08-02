#include "CCallback.h"


void CCallbackHandler::Process()
{
	Callback *callback = nullptr;
	while (m_Queue.pop(callback))
	{
		for (auto amx : m_AmxList) 
		{
			int cb_idx;
			if (amx_FindPublic(amx, callback->Name.c_str(), &cb_idx) == AMX_ERR_NONE) 
			{
				cell amx_address = -1;
				while(callback->Params.empty() == false) 
				{
					auto &param = callback->Params.top();
					if (param.type() == typeid(cell))
					{
						amx_Push(amx, boost::get<cell>(param));
					}
					else
					{
						cell tmp_addr;
						amx_PushString(amx, &tmp_addr, NULL, 
							boost::get<string>(param).c_str(), 0, 0);

						if (amx_address < NULL)
							amx_address = tmp_addr;
					}
					callback->Params.pop();
				}

				amx_Exec(amx, NULL, cb_idx);
				if(amx_address >= NULL)
					amx_Release(amx, amx_address);

				break;
			}
		}

		delete callback;
	}
}
