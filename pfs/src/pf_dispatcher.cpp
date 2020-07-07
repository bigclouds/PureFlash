#include <pf_main.h>
#include "pf_dispatcher.h"
#include "pf_message.h"


//PfDispatcher::PfDispatcher(const std::string &name) :PfEventThread(name.c_str(), IO_POOL_SIZE*3) {
//
//}

int PfDispatcher::init(int disp_index)
{
 /*
 * Why use IO_POOL_SIZE * 3 for even_thread queue size?
 * a dispatcher may let max IO_POOL_SIZE  IOs in flying. For each IO, the following events will be posted to dispatcher:
 * 1. EVT_IO_REQ when a request was received complete (CMD for read, CMD + DATA for write op) from network
 * 2. EVT_IO_COMPLETE when a request was complete by each replica, there may be 3 data replica, and 1 remote replicating
 */
	int rc = PfEventThread::init(format_string("disp_%d", disp_index).c_str(), IO_POOL_SIZE * 4);
	if(rc)
		return rc;
	rc = init_mempools();
	return rc;
}

int PfDispatcher::prepare_volume(PfVolume* vol)
{
	if (opened_volumes.find(vol->id) != opened_volumes.end())
	{
		return -EALREADY;
	}
	opened_volumes[vol->id] = vol;
	return 0;
}
int PfDispatcher::process_event(int event_type, int arg_i, void* arg_p)
{
	int rc = 0;
	switch(event_type) {
	case EVT_IO_REQ:
		rc = dispatch_io((PfServerIocb*)arg_p);
		break;
	case EVT_IO_COMPLETE:
		rc = dispatch_complete((SubTask*)arg_p);
		break;
	default:
		S5LOG_FATAL("Unknown event:%d", event_type);
	}
	return rc;
}
int PfDispatcher::dispatch_io(PfServerIocb *iocb)
{
	PfMessageHead* cmd = iocb->cmd_bd->cmd_bd;
	uint32_t shard_index = VOL_ID_TO_SHARD_INDEX(cmd->vol_id);
	PfShard * s = iocb->vol->shards[shard_index];
	iocb->task_mask = 0;
	iocb->setup_subtask(s);
	for (int i = 0; i < iocb->vol->rep_count; i++) {
		if(s->replicas[i]->status == HS_OK) {
			iocb->subtasks[i]->rep = s->replicas[i];
			s->replicas[i]->submit_io(&iocb->io_subtasks[i]);
		}
	}
	return 0;
}
int PfDispatcher::dispatch_complete(SubTask* sub_task)
{
	PfServerIocb* iocb = sub_task->parent_iocb;
	S5LOG_DEBUG("complete subtask:%p, status:%d, task_mask:0x%x, parent_io mask:0x%x, io_cid:%d", sub_task, sub_task->complete_status,
			sub_task->task_mask, iocb->task_mask, iocb->cmd_bd->cmd_bd->command_id);
	iocb->task_mask &= (~sub_task->task_mask);
	iocb->complete_status = (iocb->complete_status == PfMessageStatus::MSG_STATUS_SUCCESS ? sub_task->complete_status : iocb->complete_status);
	iocb->dec_ref(); //added in setup_subtask
	if(iocb->task_mask == 0){
		PfMessageReply* reply_bd = iocb->reply_bd->reply_bd;
		PfMessageHead* cmd_bd = iocb->cmd_bd->cmd_bd;
		reply_bd->command_id = cmd_bd->command_id;
		reply_bd->status = iocb->complete_status;
		reply_bd->command_seq = cmd_bd->command_seq;
		iocb->conn->post_send(iocb->reply_bd);
	}
	return 0;
}

int PfDispatcher::init_mempools()
{
	int pool_size = IO_POOL_SIZE;
	int rc = 0;
	rc = cmd_pool.init(sizeof(PfMessageHead), pool_size * 2);
	if (rc)
		goto release1;
	rc = data_pool.init(MAX_IO_SIZE, pool_size * 2);
	if (rc)
		goto release2;
	rc = reply_pool.init(sizeof(PfMessageReply), pool_size * 2);
	if (rc)
		goto release3;
	rc = iocb_pool.init(pool_size * 2);
	if(rc)
		goto release4;
	for(int i=0;i<pool_size*2;i++)
	{
		PfServerIocb *cb = iocb_pool.alloc();
		cb->cmd_bd = cmd_pool.alloc();
		cb->cmd_bd->data_len = sizeof(PfMessageHead);
		cb->cmd_bd->server_iocb = cb;
		cb->data_bd = data_pool.alloc();
		//data len of data_bd depends on length in message head
		cb->data_bd->server_iocb = cb;

		cb->reply_bd = reply_pool.alloc();
		cb->reply_bd->data_len =  sizeof(PfMessageReply);
		cb->reply_bd->server_iocb = cb;
		for(int i=0;i<3;i++) {
			cb->subtasks[i] = &cb->io_subtasks[i];
			cb->subtasks[i]->rep_index =i;
			cb->subtasks[i]->task_mask = 1 << i;
			cb->subtasks[i]->parent_iocb = cb;
		}
		//TODO: still 2 subtasks not initialized, for metro replicating and rebalance
		iocb_pool.free(cb);
	}
	return rc;
release4:
	reply_pool.destroy();
release3:
	data_pool.destroy();
release2:
	cmd_pool.destroy();
release1:
	return rc;
}
