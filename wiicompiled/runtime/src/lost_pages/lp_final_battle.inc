// Included inside the battle engine's namespace: final-boss turns share its
// guard, damage, status, popup and turn-order machinery.
void SpawnFinalHelpers(Guest& g) {
    const int count=s.finalBoss==1?3:1;
    if(s.enemies.size()<static_cast<size_t>(count+1))s.enemies.resize(count+1);
    for(int i=1;i<=count;++i) {
        auto& e=s.enemies[i];e=Enemy{};e.present=false;e.npc=0;
        e.name=s.finalBoss==1?"Void Sprite":"Mini Luigis";
        e.hp=e.maxHp=s.finalBoss==1?3:30;e.attack=1;e.level=40;e.ranged=true;
        e.slot=e.pos=e.start={s.marioHome.x+90.f+(i-1)*35.f,s.groundY+(s.finalBoss==1?30.f:0.f),s.marioHome.z};
        Effect(e.pos,e.pos,36,s.finalBoss==1?ColBlue:ColGreen,40);
    }
}
// The story's own Bleck plays his attacks by name on his pose: A_1a-c casting, A_4a-g the cape opening, the
// void's suction and the swallow. (Super Dimentio's parts keep their native idle.)
void FinalAnim(Guest& g,const char* name) {
    Enemy& e=s.enemies[0];
    if(s.finalBoss!=1 || !e.present || e.namedAnim==name)return;
    if(GuestString(e.npc+0x48,32).rfind("e_jigen",0)!=0)return;
    const uint32_t pose=Memory::Read32(e.npc+0x44),anim=PersistentString(g,name);
    if(pose>=4096 || !anim)return;
    e.namedAnim=name;
    g.Call(0x800432F8,{pose,anim,1});  // animPoseSetAnim
}

void TickFinalBoss(Guest& g,uint32_t pressed) {
    auto& boss=s.enemies[0];const bool swarm=s.enemyTurn==1 && s.finalBoss==2;
    if(swarm && (s.enemies.size()<2 || !s.enemies[1].alive)) {
        // The swarm can be destroyed before its turn: skip it instead of
        // letting a dead actor jump on Mario.
        s.enemyTurn=NextAlive(2);s.guard=0;s.lockout=false;StartStage(0);return;
    }
    if(s.stage==0 && s.st==1) {
        s.splitDodges=0;s.splitPenalty=0;
        if(!swarm) {
            if(boss.status[Sleep] || boss.status[Freeze]) {
                Pop({boss.pos.x,boss.pos.y+boss.height+20,boss.pos.z},boss.status[Freeze]?"Frozen!":"Zzz...",ColBlue,1);
                s.enemyTurn=NextAlive(1);s.guard=0;s.lockout=false;StartStage(0);return;
            }
            s.finalAttack=FinalBossRules::AttackForTurn(s.finalBoss,s.phase2,s.finalTurn);
            ++s.finalTurn;
            RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] kind %d phase %d attack %d helpers %zu\n",s.finalBoss,s.phase2?2:1,s.finalAttack,s.enemies.size()-1);
        }
    }
    if(s.stage==0){if(s.st>=25)StartStage(1);return;}
    const bool absorbing=!swarm && s.finalBoss==1 && s.phase2 && s.finalAttack==5;
    if(!swarm && s.stage==1) {
        if(s.st==1)FinalAnim(g,absorbing?"A_4a":"A_1a");
        if(s.st==14)FinalAnim(g,absorbing?"A_4b":"A_1b");
    }
    if(!swarm && s.stage==2) {
        if(s.st==1)FinalAnim(g,absorbing?"A_4c":"A_1c");
        if(s.st==18)FinalAnim(g,absorbing?"A_4g":"S_1");
        if(s.st==34)FinalAnim(g,"S_1");
    }
    const bool ultra=!swarm && s.phase2 && (s.finalBoss==1?s.finalAttack==6:s.finalAttack==5);
    const int impact=ultra?110:36;
    if(s.stage==1) {
        if(ultra && s.finalBoss==1) {
            // Reality Split: B on each of the three white flashes. A press outside a flash's two-frame window
            // jams the dodge for a moment, so mashing can't get through.
            if(s.splitPenalty>0)--s.splitPenalty;
            bool inWindow=false;
            for(int flash:{40,64,88}) {
                if(s.st==flash-1 || s.st==flash)inWindow=true;
                if(s.st==flash+1)s.lockout=false;
                if(s.st>=flash && s.st<flash+4)s.whiteFlash=1.f-(s.st-flash)/4.f;
            }
            if(pressed&(Btn::one|Btn::b)) {
                if(inWindow && !s.lockout && !s.splitPenalty){++s.splitDodges;s.lockout=true;Pop(s.marioHome,"Dodge!",ColBlue,1);s.guardPose=10;}
                else if(!inWindow)s.splitPenalty=14;
            }
        }
        if(!ultra && !s.guard && !s.lockout) {
            const int left=impact-s.st;
            if((pressed&(Btn::one|Btn::b)) && left<=BattleWindow(3,0,Has(g,BadgeId::Unsimplifier)))s.guard=2;
            else if((pressed&(Btn::two|Btn::a)) && left<=BattleWindow(8,Has(g,BadgeId::Simplifier),Has(g,BadgeId::Unsimplifier)))s.guard=1;
            else if(pressed&(Btn::two|Btn::one|Btn::a|Btn::b))s.lockout=true;
            if(s.guard)s.guardPose=22;
        }
        if(s.st==1)Sound(g,swarm?"SFX_BS_DMNL_DOTRUIGI_JUMP1":s.finalBoss==1?"SFX_BS_ZIGEN_ANKOKU_ATTACK1":s.finalAttack==2?"SFX_BS_DMNL_S_JUMP1":"SFX_E_MAHOU_ATTACK2");
        if(s.st==1)Effect(boss.pos,s.marioHome,ultra?(s.finalBoss==1?33:34):swarm?36:s.finalAttack==1?32:31,s.finalBoss==1?ColBlue:ColGreen,impact+15);
        if(s.st<impact)return;
        if(!swarm && s.phase2 && s.finalAttack==4){SpawnFinalHelpers(g);StartStage(2);return;}
        if(!swarm && s.finalBoss==1 && s.phase2 && s.finalAttack==5) {
            int count=0;
            for(size_t i=1;i<s.enemies.size();++i)if(s.enemies[i].alive){++count;s.enemies[i].alive=false;s.enemies[i].hp=0;Effect(s.enemies[i].pos,boss.pos,31,ColBlue,35);}
            s.buffsAtk+=count;s.buffsDef+=count;boss.defense=s.buffsDef;
            Pop(boss.pos,"Absorb: ATK/DEF +"+std::to_string(count),ColBlue,1);StartStage(2);return;
        }
        int power=swarm?1:ultra?(s.finalBoss==1?20:24):s.finalBoss==1?(s.finalAttack==3?2:4):(s.finalAttack==2?6:5);
        power+=swarm?0:s.buffsAtk;
        // Boss hits are sized to Mario's HP (balanced around Chapter 8's ~40), so the fight works at any level.
        if(!swarm)power=std::max(1,static_cast<int>(std::lround(power*std::clamp(BattleMaxHp(g)/40.f,.3f,1.2f))));
        if(ultra && s.finalBoss==1)power=power*std::max(0,3-s.splitDodges)/3;
        const int defense=(s.defending?1:0)+Has(g,BadgeId::DefendPlus)+s.boonDefense-(s.debuffTurns?2:0);
        int damage=s.guard==2?0:CombatRules::Incoming(power,defense,s.guard?1+Has(g,BadgeId::DamageDodge):0,Has(g,BadgeId::DoublePain),MarioHp(g)<=5?Has(g,BadgeId::LastStand):0);
        if(s.pureShield && damage)damage=1;
        const int hits=swarm?std::clamp(BattleMaxHp(g)/8,2,6):1;  // fewer Luigis land on a low-HP Mario
        for(int i=0;i<hits;++i){HeroHurt(g,damage);Pop({s.marioHome.x+i*5.f,s.groundY+45+i*10.f,s.marioHome.z},std::to_string(damage),ColRed,0);}
        if(s.guard){Effect(s.marioHome,s.marioHome,s.guard==2?1:0,ColBlue,25);Sound(g,"SFX_I_AC_BUTTON_OK1");}
        if(!swarm && s.finalBoss==1 && s.finalAttack==3){s.slowTurns=2;Pop(s.marioHome,"Time slowed!",ColBlue,1);}
        if(ultra && s.finalBoss==2 && !s.pureShield){s.debuffTurns=4;Pop(s.marioHome,"ATK / DEF sharply lowered",ColBlue,1);}
        RT_LOGF(RT_TAG_RUNTIME,"[lpfinal] damage %d hits %d shield %d dodges %d\n",damage,hits,s.pureShield,s.splitDodges);
        StartStage(2);return;
    }
    if(s.st>=35) {
        s.whiteFlash=0;
        // The ultimate attacks only yield to Pure Shield, so the boss telegraphs them a turn ahead.
        if(!swarm && s.phase2) {
            const int next=FinalBossRules::AttackForTurn(s.finalBoss,true,s.finalTurn);
            if(next==(s.finalBoss==1?6:5))
                Pop({boss.pos.x,boss.pos.y+boss.height+30,boss.pos.z},s.finalBoss==1?"Reality begins to split...":"The void gathers...",ColBlue,1);
        }
        s.enemyTurn=NextAlive(s.enemyTurn+1);s.guard=0;s.lockout=false;StartStage(0);
    }
}
