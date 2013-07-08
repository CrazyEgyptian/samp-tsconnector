#include "main.h"
#include "thread.h"

#include "CTeamspeak.h"
#include "CCallback.h"

#include <map>

using std::map;

bool ThreadAlive = true;
bool NotifyEventSended = false;

struct SClientInfo {
	string UID;
	string LastChannelName;
};

#ifdef _WIN32
DWORD __stdcall SocketThread(LPVOID lpParam)
#else
void * SocketThread(void *lpParam)
#endif

{
	map<int, SClientInfo*> ClientInfo;
	
	while(ThreadAlive) {
		RetryConnect:
		while(!TSServer.IsConnected()) {
			TSServer.ConnectT();
			SLEEP(50);
		}
			
		while(!TSServer.IsLoggedIn()) {
			if(!TSServer.IsConnected())
				goto RetryConnect;
			TSServer.LoginT();
			SLEEP(50);
		}
		//check if connection is alive
		if(TSServer.Send(" ") == 0) {
			CommandList *Commands = NULL;
			while( (Commands = TSServer.GetNextCommandList()) != NULL) {
			
				string MyVal[9];
				while(!Commands->empty()) {
					string RecvStr, RecvStrTmp;
					CCommand *Cmd = Commands->front();
					Commands->pop();
				
					//replace <X> with real parameters
					size_t ParamPos = 0; 
					while((ParamPos = Cmd->Command.find_first_of("<", ParamPos)) != -1) {
						int TemplateParamLen = Cmd->Command.find_first_of(">", ParamPos)-ParamPos;
						string substr = Cmd->Command.substr(ParamPos+1, TemplateParamLen-1);
						unsigned int ParamIndex = boost::lexical_cast<unsigned int>(substr) - 1;
						if(ParamIndex < (sizeof(MyVal)/sizeof(MyVal[0])) && MyVal[ParamIndex].length() > 0)
							Cmd->Command.replace(ParamPos, TemplateParamLen+1, MyVal[ParamIndex]);
						else 
							printf("[ERROR] An error occurred in Teamspeak Connector while processing a parameter.\n");
					}

					//send and recieve data
					if(TSServer.Send(Cmd->Command) != 0) {
						TSServer.AddCommandListToQueue(Commands);
						break;
					}
					int ErrorID = 0;
					while( (ErrorID = TSServer.Recv(&RecvStrTmp)) != SOCKET_ERROR && ErrorID != -2) {
						RecvStr.append(RecvStrTmp);
						RecvStrTmp.clear();
					}
					if(ErrorID == -2) {
						TSServer.AddCommandListToQueue(Commands);
						break;
					}

					//process parameter
					if(Cmd->MFind.length() > 0) {
						size_t SearchPos = 0;
						if( (SearchPos = RecvStr.find(Cmd->MFind)) != -1) {
							size_t LastDelim = RecvStr.find_first_of("|", SearchPos);
							if(LastDelim == -1)
								LastDelim = RecvStr.length();

							string tmp = RecvStr.substr(0, LastDelim);
							size_t FirstDelim = tmp.find_last_of("|");
							if(FirstDelim == -1)
								FirstDelim = 0;
							string TargetStr = RecvStr.substr(FirstDelim, LastDelim-FirstDelim);

							RecvStr = TargetStr;
						}
					}
					if(Cmd->ValName.length() > 0) {
						//stringstream RxValBuf;
						//RxValBuf << Cmd->ValName << "=([^ ]+)";
						string RxValBuf(Cmd->ValName);
						RxValBuf.append("=([^ ]+)");

						boost::regex rx(RxValBuf);
						boost::match_results<std::string::const_iterator> rxRes;
						if(boost::regex_search(RecvStr, rxRes, rx)) {
							for(unsigned int Idx=0, Max = sizeof(MyVal)/sizeof(MyVal[0]); Idx < Max; ++Idx) {
								if(MyVal[Idx].length() == 0) {
									MyVal[Idx] = rxRes[1];
									break;
								}
							}
						}
					}
					delete Cmd;
					Cmd = NULL;
				}

				delete Commands;
				Commands = NULL;
			}


			//callback shit
			if(TSServer.IsLoggedIn()) { 
				if(NotifyEventSended == false) {
					string cmd;
					
					cmd = "servernotifyregister event=channel id=0\n";
					TSServer.Send(cmd);
					while(TSServer.Recv(&cmd) != SOCKET_ERROR) {}

					cmd = "servernotifyregister event=textchannel\n";
					TSServer.Send(cmd);
					while(TSServer.Recv(&cmd) != SOCKET_ERROR) {}

					cmd = "servernotifyregister event=textserver\n";
					TSServer.Send(cmd);
					while(TSServer.Recv(&cmd) != SOCKET_ERROR) {}

					//fill ClientInfo
					map<int, string> ChannelNames;
					string 
						ClientRecv,
						ChannelRecv,
						RecvTmp;

				
					TSServer.Send("channellist");
					while(TSServer.Recv(&RecvTmp) != SOCKET_ERROR) {
						ChannelRecv.append(RecvTmp);
						RecvTmp.clear();
					}
					boost::regex rx1("cid=([^ ]+) pid=[^ ]+ channel_order=[^ ]+ channel_name=([^ ]+)");
					boost::regex_iterator<string::const_iterator> RxIter1(ChannelRecv.begin(), ChannelRecv.end(), rx1);
					boost::regex_iterator<string::const_iterator> RxEnd1;
				
					for( ;RxIter1 != RxEnd1; ++RxIter1) {
						string ChannelName = (*RxIter1)[2];
						TSServer.UnEscapeString(ChannelName);
						int ChannelID = boost::lexical_cast<int>((*RxIter1)[1]);

						ChannelNames.insert( map<int, string>::value_type(ChannelID, ChannelName) );
					}



					TSServer.Send("clientlist -uid");
					while(TSServer.Recv(&RecvTmp) != SOCKET_ERROR) {
						ClientRecv.append(RecvTmp);
						RecvTmp.clear();
					}
					boost::regex rx2("clid=([^ ]+) cid=([^ ]+) client_database_id=[^ ]+ client_nickname=[^ ]+ client_type=0 client_unique_identifier=([^=]+)");
					boost::regex_iterator<string::const_iterator> RxIter2(ClientRecv.begin(), ClientRecv.end(), rx2);
					boost::regex_iterator<string::const_iterator> RxEnd2;
				
					for( ;RxIter2 != RxEnd2; ++RxIter2) {
						string UID = (*RxIter2)[3];
						TSServer.UnEscapeString(UID);
						UID.append("=");
						int 
							ChannelID = boost::lexical_cast<int>((*RxIter2)[2]), 
							ClientID = boost::lexical_cast<int>((*RxIter2)[1]);

						SClientInfo *Client = new SClientInfo;
						Client->UID = UID;
						if(ChannelNames.find(ChannelID) != ChannelNames.end())
							Client->LastChannelName = ChannelNames.at(ChannelID);
						ClientInfo.insert( map<int, SClientInfo*>::value_type(ClientID, Client) );
					}

					NotifyEventSended = true;
				}

			
				string RecvStr;
				if(TSServer.Recv(&RecvStr) != SOCKET_ERROR) {

					if(RecvStr.find("notifycliententerview") != -1) {
						boost::regex rx("notifycliententerview cfid=[^ ]+ ctid=([^ ]+) reasonid=[^ ]+ clid=([^ ]+) client_unique_identifier=([^ ]+) client_nickname=([^ ]+)");
						boost::match_results<std::string::const_iterator> rxRes;
						if(boost::regex_search(RecvStr, rxRes, rx)) {
							int 
								ChannelID = boost::lexical_cast<int>(rxRes[1]),
								ClientID = boost::lexical_cast<int>(rxRes[2]);
							string
								UID = rxRes[3],
								Nickname = rxRes[4],
								ChannelName;
							TSServer.UnEscapeString(UID);
							TSServer.UnEscapeString(Nickname);


							char SendCmd[32];
							sprintf(SendCmd, "channelinfo cid=%d", ChannelID);
							TSServer.Send(SendCmd);
							string RecvStrTmp, RecvStr2;
							while(TSServer.Recv(&RecvStrTmp) != SOCKET_ERROR) {
								RecvStr2.append(RecvStrTmp);
								RecvStrTmp.clear();
							}

							boost::regex subrx("channel_name=([^ ]+)");
							boost::match_results<std::string::const_iterator> subrxRes;
							if(boost::regex_search(RecvStr2, subrxRes, subrx)) {
								ChannelName = subrxRes[1];
								TSServer.UnEscapeString(ChannelName);
							}

							SClientInfo *Client = new SClientInfo;
							Client->LastChannelName = ChannelName;
							Client->UID = UID;
							ClientInfo.insert( map<int, SClientInfo*>::value_type(ClientID, Client) );

							CCallback *Callback = new CCallback;
							Callback->Name = "TSC_OnClientConnect";
							Callback->Params.push(Nickname);
							Callback->Params.push(UID);
							CCallback::AddCallbackToQueue(Callback);
							Callback = NULL;
						}
					}
					else if(RecvStr.find("notifyclientleftview") != -1) {
						boost::regex rx("notifyclientleftview.*clid=([^ \n\r]+)");
						boost::match_results<std::string::const_iterator> rxRes;
						if(boost::regex_search(RecvStr, rxRes, rx)) {
							int ClientID = boost::lexical_cast<int>(rxRes[1]);

							if(ClientInfo.find(ClientID) != ClientInfo.end()) {
								SClientInfo *Client = ClientInfo.at(ClientID);
								string UID = Client->UID;
								ClientInfo.erase(ClientID);
								delete Client;
								Client = NULL;

								string Nickname;
								char SendCmd[64];
								sprintf(SendCmd, "clientgetnamefromuid cluid=%s", UID.c_str());
								TSServer.Send(SendCmd);
								string RecvStrTmp, RecvStr2;
								while(TSServer.Recv(&RecvStrTmp) != SOCKET_ERROR) {
									RecvStr2.append(RecvStrTmp);
									RecvStrTmp.clear();
								}

								boost::regex subrx("name=([^ \n\r]+)");
								boost::match_results<std::string::const_iterator> subrxRes;
								if(boost::regex_search(RecvStr2, subrxRes, subrx)) {
									Nickname = subrxRes[1];
									TSServer.UnEscapeString(Nickname);
								}


								CCallback *Callback = new CCallback;
								Callback->Name = "TSC_OnClientDisconnect";
								Callback->Params.push(Nickname);
								Callback->Params.push(UID);
								CCallback::AddCallbackToQueue(Callback);
								Callback = NULL;

							}
						}
					}
					else if(RecvStr.find("notifytextmessage") != -1) {
						boost::regex rx("notifytextmessage targetmode=([^ ]+) msg=([^ ]+) invokerid=([^ ]+) invokername=([^ ]+) invokeruid=([^ ]+)");
						boost::match_results<std::string::const_iterator> rxRes;
						if(boost::regex_search(RecvStr, rxRes, rx)) {
							string
								NickName = rxRes[4],
								UID = rxRes[5],
								Message = rxRes[2];
							TSServer.UnEscapeString(NickName);
							TSServer.UnEscapeString(UID);
							TSServer.UnEscapeString(Message);
							int 
								TargetMode = boost::lexical_cast<int>(rxRes[1]),
								ClientID = boost::lexical_cast<int>(rxRes[3]);

							if(TargetMode == 3) { //ServerText
								CCallback *Callback = new CCallback;
								Callback->Name = "TSC_OnClientServerText";
								Callback->Params.push(NickName);
								Callback->Params.push(UID);
								Callback->Params.push(Message);
								CCallback::AddCallbackToQueue(Callback);
								Callback = NULL;
							}
							else if(TargetMode == 2) { //ChannelText
								if(ClientInfo.find(ClientID) != ClientInfo.end() && NickName.length() && UID.length() && Message.length()) {
									SClientInfo *Client = ClientInfo.at(ClientID);
									CCallback *Callback = new CCallback;
									Callback->Name = "TSC_OnClientChannelText";
									Callback->Params.push(NickName);
									Callback->Params.push(UID);
									Callback->Params.push(Client->LastChannelName);
									Callback->Params.push(Message);
									CCallback::AddCallbackToQueue(Callback);
									Callback = NULL;
								}
							}
						
						}
					}
					else if(RecvStr.find("notifyclientmoved") != -1) {
						boost::regex rx("notifyclientmoved ctid=([^ ]+).*clid=([^ \n\r]+)");
						boost::match_results<std::string::const_iterator> rxRes;
						if(boost::regex_search(RecvStr, rxRes, rx)) {
							
							int 
								ClientID = boost::lexical_cast<int>(rxRes[2].str().c_str()),
								ChannelID = boost::lexical_cast<int>(rxRes[1].str().c_str());

							string ChannelName, Nickname;
							char SendCmd[32];
							sprintf(SendCmd, "channelinfo cid=%d", ChannelID);
							TSServer.Send(SendCmd);

							sprintf(SendCmd, "clientinfo clid=%d", ClientID);
							TSServer.Send(SendCmd);

							string RecvStrTmp, RecvStr2;
							while(TSServer.Recv(&RecvStrTmp) != SOCKET_ERROR) {
								RecvStr2.append(RecvStrTmp);
								RecvStrTmp.clear();
							}

							boost::regex subrx("channel_name=([^ ]+)");
							boost::match_results<std::string::const_iterator> subrxRes;
							if(boost::regex_search(RecvStr2, subrxRes, subrx)) {
								ChannelName = subrxRes[1];
								TSServer.UnEscapeString(ChannelName);
							}

							boost::regex subrx2("client_nickname=([^ ]+)");
							boost::match_results<std::string::const_iterator> subrxRes2;
							if(boost::regex_search(RecvStr2, subrxRes2, subrx2)) {
								Nickname = subrxRes2[1];
								TSServer.UnEscapeString(Nickname);
							}
						

							if(ClientInfo.find(ClientID) != ClientInfo.end() && Nickname.length() && ChannelName.length()) {
								SClientInfo *Client = ClientInfo.at(ClientID);
								Client->LastChannelName = ChannelName;
								string UID = Client->UID;

								CCallback *Callback = new CCallback;
								Callback->Name = "TSC_OnClientChannelMove";
								Callback->Params.push(Nickname);
								Callback->Params.push(UID);
								Callback->Params.push(ChannelName);
								CCallback::AddCallbackToQueue(Callback);
								Callback = NULL;
							}
						}
					}
				}
			}
		} //check if alive
		else { //reconnect
			NotifyEventSended = false;
			if(TSServer.ConnectT() == false || TSServer.LoginT() != 0)
				logprintf("[ERROR] Teamspeak Connector could not connect to Teamspeak server.");
		} 
		SLEEP(50);
	}
	return NULL;
}