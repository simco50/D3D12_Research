
#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 8), visibility = SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_ALL), "

struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
};

struct CS_INPUT
{
	uint3 GroupId : SV_GROUPID;
	uint3 GroupThreadId : SV_GROUPTHREADID;
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
	uint GroupIndex : SV_GROUPINDEX;
};

#define DEAD_LIST_COUNTER 0
#define ALIVE_LIST_1_COUNTER 4
#define ALIVE_LIST_2_COUNTER 8
#define EMIT_COUNT 12

cbuffer SimulationParameters : register(b0)
{
    uint cEmitCount;
}

cbuffer EmitParameters : register(b0)
{
    float4 cRandomDirections[64];
}

cbuffer SimulateParameters : register(b0)
{
    float cDeltaTime;
    float cParticleLifeTime;
}

RWByteAddressBuffer uCounters : register(u0);
RWByteAddressBuffer uEmitArguments : register(u1);
RWByteAddressBuffer uSimulateArguments : register(u2);
RWByteAddressBuffer uDrawArgumentsBuffer : register(u3);
RWStructuredBuffer<uint> uDeadList : register(u4);
RWStructuredBuffer<uint> uAliveList1 : register(u5);
RWStructuredBuffer<uint> uAliveList2 : register(u6);
RWStructuredBuffer<ParticleData> uParticleData  : register(u7);

ByteAddressBuffer tCounters : register(t0);

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void UpdateSimulationParameters(CS_INPUT input)
{
    uint deadCount = uCounters.Load(DEAD_LIST_COUNTER);
    uint aliveParticleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);

    uint emitCount = min(deadCount, cEmitCount);

    uEmitArguments.Store3(0, uint3(ceil((float)emitCount / 128), 1, 1));

    uint simulateCount = ceil((float)(aliveParticleCount + emitCount) / 128);
    uSimulateArguments.Store3(0, uint3(simulateCount, 1, 1));

    uCounters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
    uCounters.Store(ALIVE_LIST_2_COUNTER, 0);
    uCounters.Store(EMIT_COUNT, emitCount);
}

[numthreads(128, 1, 1)]
void Emit(CS_INPUT input)
{
    uint emitCount = uCounters.Load(EMIT_COUNT);
    if(input.DispatchThreadId.x < emitCount)
    {
        uint deadSlot;
        uCounters.InterlockedAdd(DEAD_LIST_COUNTER, -1, deadSlot);
        uint particleIndex = uDeadList[deadSlot - 1];

        ParticleData p;
        p.LifeTime = 0;
        p.Position = float3(0, 0, 0);
        p.Velocity = 20*cRandomDirections[particleIndex % 64].xyz;
        
        uParticleData[particleIndex] = p;

        uint aliveSlot;
        uCounters.InterlockedAdd(ALIVE_LIST_1_COUNTER, 1, aliveSlot);
        uAliveList1[aliveSlot] = particleIndex;
    }
}

[numthreads(128, 1, 1)]
void Simulate(CS_INPUT input)
{
    uint aliveCount = uCounters.Load(ALIVE_LIST_1_COUNTER);
    if(input.DispatchThreadId.x < aliveCount)
    {
        uint particleIndex = uAliveList1[input.DispatchThreadId.x];
        ParticleData p = uParticleData[particleIndex];

        if(p.LifeTime < cParticleLifeTime)
        {
            p.Velocity += float3(0, -9.81f * cDeltaTime, 0);
            p.Position += p.Velocity * cDeltaTime;
            p.LifeTime += cDeltaTime;
            uParticleData[particleIndex] = p;

            uint aliveSlot;
            uCounters.InterlockedAdd(ALIVE_LIST_2_COUNTER, 1, aliveSlot);
            uAliveList2[aliveSlot] = particleIndex;
        }
        else
        {
            uint deadSlot;
            uCounters.InterlockedAdd(DEAD_LIST_COUNTER, 1, deadSlot);
            uDeadList[deadSlot] = particleIndex;
        }
    }
}

[numthreads(1, 1, 1)]
void SimulateEnd(CS_INPUT input)
{
    uint particleCount = tCounters.Load(ALIVE_LIST_2_COUNTER);
    uDrawArgumentsBuffer.Store4(0, uint4(6 * particleCount, 1, 0, 0));
}